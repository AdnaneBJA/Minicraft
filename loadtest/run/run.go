// Package run drives a load test: it starts the bots on a schedule (steady, ramp or soak), prints a line a second,
// and gathers everything into a report.Result.
package run

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"sync"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/bot"
	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
	"github.com/AdnaneBJA/Minicraft/loadtest/procstat"
	"github.com/AdnaneBJA/Minicraft/loadtest/report"
)

// Options is a run.
type Options struct {
	URL         string
	Bots        int           // steady/soak: how many; ramp: the most to try
	Ramp        time.Duration // steady/soak: how long to take adding them
	Duration    time.Duration // steady/soak: how long to hold once all are in
	Mode        string        // "steady", "ramp" or "soak"
	Step        int           // ramp: bots added per step
	Every       time.Duration // ramp: time per step
	MaxP99      time.Duration // ramp: a step whose p99 input latency goes over this is the breaking point
	ProbeEvery  time.Duration // soak: a fresh player joins this often, to time joining
	ServerPID   int32         // optional: the server's process, to sample its CPU and memory
	Seed        int64
	AllowRemote bool          // allow a target that isn't on this machine or a private network
	ChangeEvery time.Duration // bots: average time between key changes (0: the bot default)
	ChatEvery   time.Duration // bots: average time between chat lines (0: the bot default)
	JoinTimeout time.Duration // bots: a join taking longer failed (0: the bot default, 10 s)
}

// ErrRemoteTarget: the URL isn't local; load-testing someone's live server needs an explicit flag.
var ErrRemoteTarget = errors.New("target is not on this machine or a private network")

// CheckTarget accepts ws:// and wss:// URLs on localhost, loopback or private (RFC 1918) addresses, and any other
// host only with allowRemote.
func CheckTarget(rawURL string, allowRemote bool) error {
	u, err := url.Parse(rawURL)
	if err != nil || (u.Scheme != "ws" && u.Scheme != "wss") || u.Hostname() == "" {
		return fmt.Errorf("not a ws:// or wss:// URL: %q", rawURL)
	}
	if allowRemote {
		return nil
	}
	host := u.Hostname()
	if host == "localhost" {
		return nil
	}
	if ip := net.ParseIP(host); ip != nil && (ip.IsLoopback() || ip.IsPrivate()) {
		return nil
	}
	return fmt.Errorf("%w: %s (pass --i-know-this-is-not-local if it's a server you own and meant to load)", ErrRemoteTarget, host)
}

type runner struct {
	o      Options
	shared *bot.Shared
	botCtx context.Context
	wg     sync.WaitGroup
	bots   int // started so far

	mu     sync.Mutex
	probes []report.Probe
	start  time.Time
}

func (r *runner) startBot(name string, seed int64, joinOnly bool) {
	cfg := bot.Config{URL: r.o.URL, Name: name, Seed: seed, ChangeEvery: r.o.ChangeEvery, ChatEvery: r.o.ChatEvery,
		JoinOnly: joinOnly, JoinTimeout: r.o.JoinTimeout}
	if joinOnly {
		cfg.OnJoined = func(took time.Duration, historyLen int) {
			r.mu.Lock()
			defer r.mu.Unlock()
			r.probes = append(r.probes, report.Probe{At: time.Since(r.start), Join: took, HistoryLen: historyLen})
		}
	}
	r.wg.Add(1)
	go func() {
		defer r.wg.Done()
		_ = bot.Run(r.botCtx, cfg, r.shared)
	}()
}

func (r *runner) addBot() {
	r.bots++
	r.startBot(fmt.Sprintf("bot%03d", r.bots), r.o.Seed+int64(r.bots), false)
}

// wait sleeps for d, or until the run is cancelled (false).
func wait(ctx context.Context, d time.Duration) bool {
	select {
	case <-ctx.Done():
		return false
	case <-time.After(d):
		return true
	}
}

// Run runs the load test, printing a line a second to live. Cancelling ctx stops early and still returns what was
// measured (Cancelled set).
func Run(ctx context.Context, o Options, live io.Writer) (report.Result, error) {
	if err := CheckTarget(o.URL, o.AllowRemote); err != nil {
		return report.Result{}, err
	}
	if o.Bots <= 0 {
		return report.Result{}, errors.New("--bots must be at least 1")
	}
	if o.Mode == "" {
		o.Mode = "steady"
	}
	if o.Mode == "ramp" && (o.Step <= 0 || o.Every <= 0) {
		return report.Result{}, errors.New("ramp mode needs --step and --every")
	}
	if o.Mode != "steady" && o.Mode != "ramp" && o.Mode != "soak" {
		return report.Result{}, fmt.Errorf("unknown mode %q (steady, ramp or soak)", o.Mode)
	}

	botCtx, stopBots := context.WithCancel(context.Background())
	defer stopBots()
	r := &runner{
		o: o,
		shared: &bot.Shared{Latency: metrics.NewHistogram(), Jitter: metrics.NewHistogram(),
			Join: metrics.NewHistogram(), Counters: &metrics.Counters{}},
		botCtx: botCtx,
		start:  time.Now(),
	}
	result := report.Result{
		Config: report.Config{URL: o.URL, Mode: o.Mode, Bots: o.Bots, Ramp: o.Ramp, Duration: o.Duration,
			Step: o.Step, Every: o.Every, MaxP99: o.MaxP99},
		StartedAt: r.start,
	}

	var sampler *procstat.Sampler
	if o.ServerPID > 0 {
		var err error
		if sampler, err = procstat.StartSampler(botCtx, o.ServerPID, time.Second); err != nil {
			return result, err
		}
	}

	liveDone := make(chan struct{})
	go r.liveLines(ctx, live, liveDone)

	switch o.Mode {
	case "ramp":
		r.ramp(ctx, &result)
	default:
		r.steady(ctx)
	}

	result.Cancelled = ctx.Err() != nil
	stopBots()
	close(liveDone)
	waitGroup(&r.wg, 5*time.Second) // bots close their connections

	result.Duration = time.Since(r.start)
	result.Latency = r.shared.Latency.Summary()
	result.Jitter = r.shared.Jitter.Summary()
	result.Join = r.shared.Join.Summary()
	result.Counters = r.shared.Counters.Snapshot()
	if sampler != nil {
		stats := sampler.Stats()
		result.Server = &stats
	}
	r.mu.Lock()
	result.Probes = r.probes
	r.mu.Unlock()
	return result, nil
}

// steady (and soak): add the bots evenly over Ramp, then hold for Duration. Soak also sends a probe now and then.
func (r *runner) steady(ctx context.Context) {
	interval := time.Duration(0)
	if r.o.Bots > 1 {
		interval = r.o.Ramp / time.Duration(r.o.Bots)
	}
	for r.bots < r.o.Bots {
		r.addBot()
		if r.bots < r.o.Bots && !wait(ctx, interval) {
			return
		}
	}
	end := time.After(r.o.Duration)
	var probes <-chan time.Time
	if r.o.Mode == "soak" && r.o.ProbeEvery > 0 {
		ticker := time.NewTicker(r.o.ProbeEvery)
		defer ticker.Stop()
		probes = ticker.C
	}
	probe := 0
	for {
		select {
		case <-ctx.Done():
			return
		case <-end:
			return
		case <-probes:
			probe++
			r.startBot(fmt.Sprintf("probe%03d", probe), -int64(probe), true)
		}
	}
}

// ramp: add Step bots every Every until something gives (the breaking point) or all the bots are in.
func (r *runner) ramp(ctx context.Context, result *report.Result) {
	for {
		step := metrics.NewHistogram()
		r.shared.StepLatency.Store(step)
		before := r.shared.Counters.Snapshot()
		for i := 0; i < r.o.Step && r.bots < r.o.Bots; i++ {
			r.addBot()
		}
		if !wait(ctx, r.o.Every) {
			return
		}
		after := r.shared.Counters.Snapshot()
		// No tick for a while although bots are connected: the server stopped sending (wedged or far behind).
		silence := time.Since(time.Unix(0, r.shared.LastTick.Load()))
		silent := after.Connected > 0 && r.shared.LastTick.Load() != 0 && silence > min(time.Second, r.o.Every/2)
		superseded := after.Superseded - before.Superseded
		s := report.Step{Bots: r.bots, Latency: step.Summary(), Refused: after.Refused - before.Refused,
			Dropped: after.Dropped - before.Dropped, JoinFailed: after.JoinFailed - before.JoinFailed}
		result.Steps = append(result.Steps, s)
		switch {
		case silent:
			result.Break = &report.Break{Bots: r.bots,
				Cause: fmt.Sprintf("no ticks for %v: the server stopped responding", silence.Round(time.Millisecond))}
		case s.Refused > 0:
			result.Break = &report.Break{Bots: r.bots, Cause: fmt.Sprintf("%d connections refused", s.Refused)}
		case s.Dropped > 0:
			result.Break = &report.Break{Bots: r.bots, Cause: fmt.Sprintf("%d connections dropped", s.Dropped)}
		case s.JoinFailed > 0:
			result.Break = &report.Break{Bots: r.bots, Cause: fmt.Sprintf("%d joins refused", s.JoinFailed)}
		case superseded > 0:
			result.Break = &report.Break{Bots: r.bots,
				Cause: fmt.Sprintf("%d inputs never reached the world in time (server behind)", superseded)}
		case s.Latency.N > 0 && s.Latency.P99 > r.o.MaxP99:
			result.Break = &report.Break{Bots: r.bots,
				Cause: fmt.Sprintf("p99 input latency %v over %v", s.Latency.P99.Round(time.Millisecond), r.o.MaxP99)}
		}
		if result.Break != nil || r.bots >= r.o.Bots {
			return
		}
	}
}

func (r *runner) liveLines(ctx context.Context, live io.Writer, done <-chan struct{}) {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var lastBytes int64
	for {
		select {
		case <-ctx.Done():
			return
		case <-done:
			return
		case <-ticker.C:
			c := r.shared.Counters.Snapshot()
			fmt.Fprintln(live, report.LiveLine(time.Since(r.start), int(c.Connected), r.o.Bots,
				r.shared.Latency.Summary(), r.shared.Jitter.Summary(), c.Bytes-lastBytes, c.Dropped))
			lastBytes = c.Bytes
		}
	}
}

// waitGroup waits, but not forever: a bot stuck on a dead connection mustn't hold the report back.
func waitGroup(wg *sync.WaitGroup, limit time.Duration) {
	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(limit):
	}
}
