package probe

import (
	"context"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/internal/fakeserver"
)

func config(url string) Config {
	return Config{URL: url, Token: "secret", Pings: 5, PingEvery: 50 * time.Millisecond, Timeout: 3 * time.Second}
}

func start(t *testing.T) *fakeserver.Server {
	srv := fakeserver.Start(t)
	srv.ObserveToken = "secret"
	return srv
}

func TestOnceOK(t *testing.T) {
	srv := start(t)
	srv.PongDelay = 20 * time.Millisecond
	run := Once(context.Background(), config(srv.URL))
	if run.Result != OK {
		t.Fatalf("result %s: %v", run.Result, run.Err)
	}
	if run.Join <= 0 || run.Join > time.Second {
		t.Errorf("join %v", run.Join)
	}
	if len(run.Latencies) != 5 {
		t.Fatalf("%d latencies", len(run.Latencies))
	}
	for _, l := range run.Latencies {
		if l < 20*time.Millisecond || l > 200*time.Millisecond { // the delay, plus up to a tick and scheduling
			t.Errorf("latency %v", l)
		}
	}
	if len(run.TickGaps) == 0 {
		t.Error("no tick gaps recorded")
	}
}

func TestOnceRefused(t *testing.T) {
	srv := start(t)
	srv.RefuseObservers = true
	if run := Once(context.Background(), config(srv.URL)); run.Result != Refused {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceConnectFailed(t *testing.T) {
	if run := Once(context.Background(), config("ws://127.0.0.1:1")); run.Result != ConnectFailed {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceStalledServerTimesOut(t *testing.T) {
	srv := start(t)
	srv.Stall()
	cfg := config(srv.URL)
	cfg.Timeout = 500 * time.Millisecond
	started := time.Now()
	run := Once(context.Background(), cfg)
	if run.Result != Timeout {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
	if took := time.Since(started); took > 2*time.Second {
		t.Errorf("took %v: the deadline wasn't kept", took)
	}
}

func TestOnceLostPongsTimeOut(t *testing.T) {
	srv := start(t)
	srv.PongDelay = time.Hour // pings never answered
	cfg := config(srv.URL)
	cfg.Timeout = 700 * time.Millisecond
	if run := Once(context.Background(), cfg); run.Result != Timeout {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceSurvivesWorldReset(t *testing.T) {
	srv := start(t)
	cfg := config(srv.URL)
	cfg.PingEvery = 100 * time.Millisecond
	go func() {
		time.Sleep(150 * time.Millisecond)
		srv.ResetWorld()
	}()
	if run := Once(context.Background(), cfg); run.Result != OK {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

func TestOnceWrongTokenRefused(t *testing.T) {
	srv := start(t)
	cfg := config(srv.URL)
	cfg.Token = "guess"
	if run := Once(context.Background(), cfg); run.Result != Refused {
		t.Errorf("result %s: %v", run.Result, run.Err)
	}
}

// The server keeps one ping per observer: a newer one replaces one not answered yet. The replaced pings still get
// a latency, from the tick that answered the newer one (it would have answered them too).
func TestOnceReplacedPingsCountAtTheirAnswer(t *testing.T) {
	srv := start(t)
	cfg := config(srv.URL)
	cfg.PingEvery = 2 * time.Millisecond // several pings per tick: most are replaced
	run := Once(context.Background(), cfg)
	if run.Result != OK {
		t.Fatalf("result %s: %v", run.Result, run.Err)
	}
	if len(run.Latencies) != cfg.Pings {
		t.Fatalf("%d latencies for %d pings", len(run.Latencies), cfg.Pings)
	}
}
