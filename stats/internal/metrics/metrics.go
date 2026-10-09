// Package metrics is what the stats service shows Prometheus at /metrics: the game server's health (from the
// batches it posts), and the service's own work.
package metrics

import (
	"net/http"
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
)

// Metrics holds every metric; safe for concurrent use.
type Metrics struct {
	reg *prometheus.Registry

	mu     sync.Mutex
	lastAt int64 // the newest health applied (its batch's cut time)

	online, connections, observers, ticks, historyTicks, historyBytes, backlog, rss, cpu prometheus.Gauge
	lastReport                                                                           prometheus.Gauge
	events, batches                                                                      *prometheus.CounterVec
	dbWrite                                                                              prometheus.Histogram
}

// New registers every metric, plus the Go runtime's and the process's.
func New() *Metrics {
	m := &Metrics{reg: prometheus.NewRegistry()}
	gauge := func(name, help string) prometheus.Gauge {
		g := prometheus.NewGauge(prometheus.GaugeOpts{Name: name, Help: help})
		m.reg.MustRegister(g)
		return g
	}
	m.online = gauge("minicraft_players_online", "Players in the world.")
	m.connections = gauge("minicraft_connections", "Open WebSocket connections to the game server.")
	m.observers = gauge("minicraft_observers", "Monitoring probes watching the world.")
	m.ticks = gauge("minicraft_ticks_total", "Ticks the game server sent since it started (use rate()).")
	m.historyTicks = gauge("minicraft_history_ticks", "The current world's age in ticks.")
	m.historyBytes = gauge("minicraft_history_bytes", "What a player joining now downloads, in bytes.")
	m.backlog = gauge("minicraft_reporter_backlog", "Stats batches waiting to go out on the game server.")
	m.rss = gauge("minicraft_process_resident_bytes", "The game server's resident memory.")
	m.cpu = gauge("minicraft_process_cpu_seconds_total", "The game server's CPU time (use rate()).")
	m.lastReport = gauge("minicraft_last_report_timestamp_seconds", "When the game server's last batch arrived.")
	m.events = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "stats_events_ingested_total", Help: "Events in batches that were stored, by type."}, []string{"type"})
	m.batches = prometheus.NewCounterVec(prometheus.CounterOpts{
		Name: "stats_batches_total", Help: "Batches posted to /events, by result."}, []string{"result"})
	m.dbWrite = prometheus.NewHistogram(prometheus.HistogramOpts{
		Name: "stats_db_write_seconds", Help: "Time to store one batch.",
		Buckets: []float64{.001, .0025, .005, .01, .025, .05, .1, .25, .5, 1}})
	m.reg.MustRegister(m.events, m.batches, m.dbWrite,
		collectors.NewGoCollector(), collectors.NewProcessCollector(collectors.ProcessCollectorOpts{}))
	for _, result := range []string{"accepted", "duplicate", "invalid", "unauthorized", "error"} {
		m.batches.WithLabelValues(result) // every result shows from the start, at 0
	}
	return m
}

// Registry is where the metrics live (for tests).
func (m *Metrics) Registry() *prometheus.Registry { return m.reg }

// Handler serves the metrics in Prometheus's format.
func (m *Metrics) Handler() http.Handler { return promhttp.HandlerFor(m.reg, promhttp.HandlerOpts{}) }

// Report records a valid batch: it arrived `now`. Its health updates the gauges only if it's newer than the last one
// applied (a retried old batch must not roll them back); a batch without health only updates the online count.
func (m *Metrics) Report(online int, h *ingest.Health, now time.Time) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.lastReport.Set(float64(now.UnixMilli()) / 1000)
	if h == nil {
		m.online.Set(float64(online))
		return
	}
	if h.At <= m.lastAt {
		return
	}
	m.lastAt = h.At
	m.online.Set(float64(online))
	m.connections.Set(float64(h.Connections))
	m.observers.Set(float64(h.Observers))
	m.ticks.Set(float64(h.Ticks))
	m.historyTicks.Set(float64(h.HistoryTicks))
	m.historyBytes.Set(float64(h.HistoryBytes))
	m.backlog.Set(float64(h.Backlog))
	m.rss.Set(float64(h.RSSBytes))
	m.cpu.Set(h.CPUSeconds)
}

// Batch counts a POST to /events by its result.
func (m *Metrics) Batch(result string) { m.batches.WithLabelValues(result).Inc() }

// Events counts the events of a stored batch by type.
func (m *Metrics) Events(events []ingest.Event) {
	for _, e := range events {
		m.events.WithLabelValues(e.Type).Inc()
	}
}

// DBWrite records how long storing a batch took.
func (m *Metrics) DBWrite(d time.Duration) { m.dbWrite.Observe(d.Seconds()) }
