// probe: watches the hosted game from the outside, forever. Every --every it joins as a hidden observer, times the
// join and --pings round trips through the server's tick loop, and serves the results to Prometheus on --listen.
//
//	PROBE_TOKEN=... go run ./cmd/probe --url wss://minicraft.example.org
//
// The server lets only probes with its PROBE_TOKEN watch unseen; the token is read from the environment (or --token)
// so it never shows in a process list.
package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"math"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/AdnaneBJA/Minicraft/loadtest/probe"
)

const tickLength = time.Second / 60

func main() {
	url := flag.String("url", "", "the game's WebSocket URL (required), e.g. wss://minicraft.example.org")
	token := flag.String("token", os.Getenv("PROBE_TOKEN"), "the server's probe token (default $PROBE_TOKEN)")
	every := flag.Duration("every", 30*time.Second, "time between runs")
	pings := flag.Int("pings", 10, "pings per run")
	pingEvery := flag.Duration("ping-every", 100*time.Millisecond, "time between pings (at least a tick)")
	timeout := flag.Duration("timeout", 10*time.Second, "one run's deadline")
	listen := flag.String("listen", ":9100", "where to serve /metrics")
	flag.Parse()
	if *url == "" {
		flag.Usage()
		os.Exit(2)
	}

	reg := prometheus.NewRegistry()
	runs := prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "probe_runs_total", Help: "Probe runs, by how they ended."}, []string{"result"})
	join := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_join_seconds", Help: "From connecting to having the whole world.",
		Buckets: []float64{.01, .025, .05, .1, .25, .5, 1, 2.5, 5, 10}})
	latency := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_latency_seconds", Help: "A ping to its pong, sent after the server's next tick.",
		Buckets: []float64{.002, .005, .01, .015, .02, .03, .05, .1, .25, .5, 1}})
	jitter := prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "probe_tick_jitter_seconds", Help: "How far the gap between two ticks strays from 16.7 ms.",
		Buckets: []float64{.0005, .001, .002, .005, .01, .02, .05, .1, .5}})
	lastSuccess := prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "probe_last_success_timestamp_seconds", Help: "When a run last ended ok."})
	reg.MustRegister(runs, join, latency, jitter, lastSuccess,
		collectors.NewGoCollector(), collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}))
	for _, r := range probe.Results {
		runs.WithLabelValues(string(r)) // every result shows from the start, at 0
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	mux := http.NewServeMux()
	mux.Handle("/metrics", promhttp.HandlerFor(reg, promhttp.HandlerOpts{}))
	srv := &http.Server{Addr: *listen, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	go func() {
		if err := srv.ListenAndServe(); !errors.Is(err, http.ErrServerClosed) {
			log.Fatal(err)
		}
	}()
	log.Printf("probing %s every %v; metrics on %s", *url, *every, *listen)

	cfg := probe.Config{URL: *url, Token: *token, Pings: *pings, PingEvery: *pingEvery, Timeout: *timeout}
	ticker := time.NewTicker(*every)
	defer ticker.Stop()
	for ctx.Err() == nil {
		run := probe.Once(ctx, cfg)
		if ctx.Err() != nil {
			break
		}
		runs.WithLabelValues(string(run.Result)).Inc()
		if run.Join > 0 {
			join.Observe(run.Join.Seconds())
		}
		for _, l := range run.Latencies {
			latency.Observe(l.Seconds())
		}
		for _, g := range run.TickGaps {
			jitter.Observe(math.Abs((g - tickLength).Seconds()))
		}
		if run.Result == probe.OK {
			lastSuccess.SetToCurrentTime()
		} else {
			log.Printf("run %s: %v", run.Result, run.Err)
		}
		select {
		case <-ctx.Done():
		case <-ticker.C:
		}
	}
	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = srv.Shutdown(shutdown)
}
