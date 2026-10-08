package bot

import (
	"context"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/internal/fakeserver"
	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
)

func shared() *Shared {
	return &Shared{Latency: metrics.NewHistogram(), Jitter: metrics.NewHistogram(), Join: metrics.NewHistogram(),
		Counters: &metrics.Counters{}}
}

// runFor runs one bot until `d` passes (or it ends by itself), and returns its error.
func runFor(t *testing.T, url, name string, s *Shared, d time.Duration) error {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	return Run(ctx, Config{URL: url, Name: name, Seed: 1, ChangeEvery: 200 * time.Millisecond}, s)
}

func TestBotJoinsAndPlays(t *testing.T) {
	srv := fakeserver.Start(t)
	s := shared()
	if err := runFor(t, srv.URL, "bot001", s, 2*time.Second); err != nil {
		t.Fatalf("run: %v", err)
	}
	c := s.Counters.Snapshot()
	if c.Joined != 1 || c.Dropped != 0 || c.Connected != 0 {
		t.Errorf("counters %+v", c)
	}
	if n := s.Latency.Summary().N; n == 0 {
		t.Error("no input latency measured")
	}
	if n := s.Jitter.Summary().N; n < 50 {
		t.Errorf("only %d tick gaps measured in 2 s", n)
	}
	if s.Join.Summary().N != 1 {
		t.Error("join time not recorded")
	}
}

func TestInputLatencyMeasured(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.Delay = 50 * time.Millisecond
	s := shared()
	if err := runFor(t, srv.URL, "bot001", s, 3*time.Second); err != nil {
		t.Fatal(err)
	}
	sum := s.Latency.Summary()
	if sum.N < 3 || sum.P50 < 50*time.Millisecond || sum.P50 > 75*time.Millisecond {
		t.Errorf("latency %+v, want p50 between 50 and 75 ms", sum)
	}
}

func TestNameInUseIsJoinFailure(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.RejectNames["bot001"] = true
	s := shared()
	if err := runFor(t, srv.URL, "bot001", s, 2*time.Second); err == nil {
		t.Error("want an error")
	}
	if c := s.Counters.Snapshot(); c.JoinFailed != 1 || c.Joined != 0 {
		t.Errorf("counters %+v", c)
	}
}

func TestBotCountsDrop(t *testing.T) {
	srv := fakeserver.Start(t)
	s := shared()
	go func() {
		time.Sleep(500 * time.Millisecond)
		srv.CloseAll()
	}()
	start := time.Now()
	err := runFor(t, srv.URL, "bot001", s, 5*time.Second)
	if err == nil || time.Since(start) > 3*time.Second {
		t.Errorf("run should end soon after the drop with an error: %v after %v", err, time.Since(start))
	}
	if c := s.Counters.Snapshot(); c.Dropped != 1 || c.Connected != 0 {
		t.Errorf("counters %+v", c)
	}
}

func TestBotCountsRefusedConnection(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.RefuseConnections()
	s := shared()
	if err := runFor(t, srv.URL, "bot001", s, 2*time.Second); err == nil {
		t.Error("want an error")
	}
	if c := s.Counters.Snapshot(); c.Refused != 1 || c.Joined != 0 {
		t.Errorf("counters %+v", c)
	}
}

func TestSlowWriterDoesNotBlockReader(t *testing.T) {
	srv := fakeserver.Start(t)
	s := shared()
	done := make(chan error, 1)
	ctx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	go func() {
		done <- Run(ctx, Config{URL: srv.URL, Name: "bot001", Seed: 1, ChangeEvery: 20 * time.Millisecond}, s)
	}()
	time.Sleep(500 * time.Millisecond)
	srv.PauseReading(2 * time.Second)
	before := s.Jitter.Summary().N
	time.Sleep(1500 * time.Millisecond)
	if after := s.Jitter.Summary().N; after-before < 60 {
		t.Errorf("ticks stopped being read while the server wasn't reading: %d -> %d", before, after)
	}
	if err := <-done; err != nil {
		t.Errorf("run: %v", err)
	}
}
