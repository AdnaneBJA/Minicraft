package bot

import (
	"context"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/internal/fakeserver"
	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
	"github.com/AdnaneBJA/Minicraft/loadtest/protocol"
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

func TestJoinStreamsALargeHistory(t *testing.T) {
	// The world's history comes in one message that can be hundreds of MB after a few hours: it must not hit the
	// limit for ordinary messages, and must not be held in memory whole.
	srv := fakeserver.Start(t)
	srv.History = make([]protocol.TickMsg, 20_000)
	for i := range srv.History {
		turns := make([]protocol.Turn, 16)
		for p := range turns {
			turns[p] = protocol.Turn{PlayerID: int32(p + 1), Keys: protocol.KeyRight}
		}
		srv.History[i] = protocol.TickMsg{Tick: int32(i + 1), Turns: turns}
	}
	if size := len(protocol.EncodeJoined(7, srv.History)); size <= maxMessage {
		t.Fatalf("test history is only %d bytes; it must exceed the %d-byte message limit", size, maxMessage)
	}
	s := shared()
	var historyLen int
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	err := Run(ctx, Config{URL: srv.URL, Name: "probe001", JoinOnly: true,
		OnJoined: func(_ time.Duration, n int) { historyLen = n }}, s)
	if err != nil || historyLen != 20_000 {
		t.Fatalf("join: err %v, history %d", err, historyLen)
	}
}

func TestJoinTimesOutOnAWedgedServer(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.Stall()
	s := shared()
	start := time.Now()
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	err := Run(ctx, Config{URL: srv.URL, Name: "bot001", JoinTimeout: 500 * time.Millisecond}, s)
	if err == nil || time.Since(start) > 3*time.Second {
		t.Fatalf("want a join timeout soon, got %v after %v", err, time.Since(start))
	}
	if c := s.Counters.Snapshot(); c.JoinFailed != 1 || c.Refused != 0 {
		t.Errorf("a join that never completes is a join failure, not a refusal: %+v", c)
	}
}

func TestStaleEchoDoesNotResolveLatency(t *testing.T) {
	// The server falls 2 s behind on inputs while ticks keep coming. The bot's changes pile up (superseded), and a
	// change that happens to equal the keys the server still shows must not be "confirmed" by that stale echo.
	srv := fakeserver.Start(t)
	srv.Delay = 2 * time.Second
	s := shared()
	ctx, cancel := context.WithTimeout(context.Background(), 1800*time.Millisecond)
	defer cancel()
	_ = Run(ctx, Config{URL: srv.URL, Name: "bot001", Seed: 3, ChangeEvery: 60 * time.Millisecond}, s)
	if n := s.Latency.Summary().N; n != 0 {
		t.Errorf("%d latencies measured although no input reached the ticks in time", n)
	}
	if s.Counters.Snapshot().Superseded == 0 {
		t.Error("the piled-up changes should be counted as superseded")
	}
}
