package run

import (
	"context"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/internal/fakeserver"
	"github.com/AdnaneBJA/Minicraft/loadtest/report"
)

func TestCheckTarget(t *testing.T) {
	cases := []struct {
		url         string
		allowRemote bool
		ok          bool
	}{
		{"ws://localhost:7777", false, true},
		{"ws://127.0.0.1:7777", false, true},
		{"ws://[::1]:7777", false, true},
		{"ws://10.0.0.5:7777", false, true},
		{"ws://192.168.1.20:7777", false, true},
		{"ws://172.16.3.4:7777", false, true},
		{"wss://minicraft-adnane.duckdns.org", false, false},
		{"ws://54.236.31.78:7777", false, false},
		{"ws://54.236.31.78:7777", true, true},
		{"wss://minicraft-adnane.duckdns.org", true, true},
		{"http://localhost:7777", false, false},
		{"not a url", false, false},
		{"", false, false},
	}
	for _, c := range cases {
		err := CheckTarget(c.url, c.allowRemote)
		if (err == nil) != c.ok {
			t.Errorf("CheckTarget(%q, %v) = %v, want ok=%v", c.url, c.allowRemote, err, c.ok)
		}
	}
}

func options(url string) Options {
	return Options{URL: url, Bots: 4, Ramp: 200 * time.Millisecond, Duration: 2 * time.Second, Mode: "steady",
		Seed: 1, ChangeEvery: 150 * time.Millisecond}
}

func TestRunSteadyAgainstFake(t *testing.T) {
	srv := fakeserver.Start(t)
	r, err := Run(context.Background(), options(srv.URL), io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if r.Counters.Joined != 4 || r.Counters.Dropped != 0 || r.Latency.N == 0 || r.Jitter.N == 0 {
		t.Errorf("result %+v", r)
	}
	if r.Break != nil {
		t.Errorf("steady mode has no breaking point: %+v", r.Break)
	}
}

func TestRunRampFindsCap(t *testing.T) {
	srv := fakeserver.Start(t)
	srv.MaxPlayers = 6
	o := options(srv.URL)
	o.Mode, o.Bots, o.Step, o.Every, o.MaxP99 = "ramp", 10, 2, 400*time.Millisecond, time.Second
	r, err := Run(context.Background(), o, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if r.Break == nil || r.Break.Bots != 8 || !strings.Contains(r.Break.Cause, "refused") {
		t.Fatalf("breaking point %+v, steps %+v", r.Break, r.Steps)
	}
	if len(r.Steps) != 4 {
		t.Errorf("steps %+v", r.Steps)
	}
}

func TestRunRampReachesTarget(t *testing.T) {
	srv := fakeserver.Start(t)
	o := options(srv.URL)
	o.Mode, o.Bots, o.Step, o.Every, o.MaxP99 = "ramp", 4, 2, 400*time.Millisecond, time.Second
	r, err := Run(context.Background(), o, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if r.Break != nil || len(r.Steps) != 2 || r.Steps[1].Bots != 4 {
		t.Errorf("break %+v steps %+v", r.Break, r.Steps)
	}
}

func TestRunSoakProbesJoins(t *testing.T) {
	srv := fakeserver.Start(t)
	o := options(srv.URL)
	o.Mode, o.Bots, o.Duration, o.ProbeEvery = "soak", 2, 1500*time.Millisecond, 400*time.Millisecond
	r, err := Run(context.Background(), o, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if len(r.Probes) < 2 || r.Probes[0].Join <= 0 {
		t.Errorf("probes %+v", r.Probes)
	}
	if r.Counters.Joined < 4 { // the 2 bots and at least 2 probes
		t.Errorf("joined %d", r.Counters.Joined)
	}
}

func TestRunCancelledWritesReport(t *testing.T) {
	srv := fakeserver.Start(t)
	ctx, cancel := context.WithCancel(context.Background())
	time.AfterFunc(500*time.Millisecond, cancel)
	o := options(srv.URL)
	o.Duration = time.Minute
	start := time.Now()
	r, err := Run(ctx, o, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if time.Since(start) > 3*time.Second || !r.Cancelled || r.Counters.Joined == 0 {
		t.Fatalf("cancelled run: %+v after %v", r, time.Since(start))
	}
	base := filepath.Join(t.TempDir(), "report")
	if err := report.WriteFiles(r, base); err != nil {
		t.Fatal(err)
	}
	for _, ext := range []string{".json", ".md"} {
		if info, err := os.Stat(base + ext); err != nil || info.Size() == 0 {
			t.Errorf("%s: %v", ext, err)
		}
	}
}

func TestRunRefusesRemoteTarget(t *testing.T) {
	o := options("wss://example.com")
	if _, err := Run(context.Background(), o, io.Discard); err == nil {
		t.Error("a remote target must be refused without AllowRemote")
	}
}

func TestRunRampBreaksOnAWedgedServer(t *testing.T) {
	// The server stops answering after a few bots: nothing is refused or dropped, but nothing is measured either.
	// A ramp must call that a breaking point, not "healthy".
	srv := fakeserver.Start(t)
	o := options(srv.URL)
	o.Mode, o.Bots, o.Step, o.Every, o.MaxP99 = "ramp", 12, 4, 600*time.Millisecond, time.Second
	o.JoinTimeout = 400 * time.Millisecond
	time.AfterFunc(700*time.Millisecond, srv.Stall)
	r, err := Run(context.Background(), o, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if r.Break == nil || r.Break.Bots > 8 {
		t.Fatalf("a wedged server must be a breaking point: break %+v steps %+v", r.Break, r.Steps)
	}
}
