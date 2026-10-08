package report

import (
	"encoding/json"
	"strings"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
)

func sample() Result {
	return Result{
		Config:   Config{URL: "ws://localhost:7777", Bots: 32, Mode: "steady"},
		Duration: 3 * time.Minute,
		Latency:  metrics.Summary{N: 1000, P50: 21 * time.Millisecond, P95: 35 * time.Millisecond, P99: 48 * time.Millisecond, Max: 90 * time.Millisecond},
		Jitter:   metrics.Summary{N: 10000, P50: time.Millisecond, P99: 4 * time.Millisecond},
		Join:     metrics.Summary{N: 32, P50: 40 * time.Millisecond},
		Counters: metrics.CountersSnapshot{Joined: 32, Bytes: 3 << 20},
	}
}

func TestMarkdownReport(t *testing.T) {
	md := Markdown(sample())
	for _, want := range []string{"Input latency", "| 21 ms |", "48 ms", "Tick jitter", "32"} {
		if !strings.Contains(md, want) {
			t.Errorf("markdown misses %q:\n%s", want, md)
		}
	}
}

func TestMarkdownRampAndBreak(t *testing.T) {
	r := sample()
	r.Config.Mode = "ramp"
	r.Steps = []Step{{Bots: 4, Latency: metrics.Summary{N: 10, P99: 30 * time.Millisecond}}, {Bots: 8, Refused: 2}}
	r.Break = &Break{Bots: 8, Cause: "connections refused"}
	md := Markdown(r)
	if !strings.Contains(md, "Breaking point") || !strings.Contains(md, "connections refused") || !strings.Contains(md, "| 8 |") {
		t.Errorf("ramp markdown:\n%s", md)
	}
}

func TestJSONRoundTrip(t *testing.T) {
	b, err := JSON(sample())
	if err != nil {
		t.Fatal(err)
	}
	var back Result
	if err := json.Unmarshal(b, &back); err != nil || back.Latency.P99 != 48*time.Millisecond {
		t.Errorf("%v %+v", err, back.Latency)
	}
}

func TestLiveLine(t *testing.T) {
	line := LiveLine(42*time.Second, 30, 32, metrics.Summary{P50: 21 * time.Millisecond, P99: 48 * time.Millisecond},
		metrics.Summary{P99: 4 * time.Millisecond}, 1_900_000, 1)
	for _, want := range []string{"t=00:42", "bots=30/32", "p50=21ms", "p99=48ms", "jitter p99=4ms", "1.8 MB/s", "drops=1"} {
		if !strings.Contains(line, want) {
			t.Errorf("live line %q misses %q", line, want)
		}
	}
}

func TestCheckThresholds(t *testing.T) {
	r := sample()
	if err := Check(r, 250*time.Millisecond, true); err != nil {
		t.Errorf("healthy run failed: %v", err)
	}
	if err := Check(r, 40*time.Millisecond, false); err == nil {
		t.Error("p99 48 ms over a 40 ms limit should fail")
	}
	r.Counters.Dropped = 1
	if err := Check(r, 0, true); err == nil {
		t.Error("a drop should fail with failOnErrors")
	}
	if err := Check(r, 0, false); err != nil {
		t.Errorf("drops are fine without failOnErrors: %v", err)
	}
	empty := Result{}
	if err := Check(empty, 250*time.Millisecond, true); err == nil {
		t.Error("a run with no latency samples at all should fail the latency check")
	}
}

func TestSmallValuesStayReadable(t *testing.T) {
	if got := ms(400 * time.Microsecond); got != "0.40 ms" {
		t.Errorf("400 µs = %q", got)
	}
	if got := ms(4200 * time.Microsecond); got != "4.2 ms" {
		t.Errorf("4.2 ms = %q", got)
	}
	if got := rate(6_000); got != "5.9 KB" {
		t.Errorf("6000 B = %q", got)
	}
	if got := rate(1_900_000); got != "1.8 MB" {
		t.Errorf("1.9 MB = %q", got)
	}
	line := LiveLine(time.Second, 1, 1, metrics.Summary{P50: 300 * time.Microsecond}, metrics.Summary{}, 6_000, 0)
	if !strings.Contains(line, "p50=0.3ms") || !strings.Contains(line, "rx=5.9 KB/s") {
		t.Errorf("live line %q", line)
	}
}

func TestCheckFailsOnSupersededInputs(t *testing.T) {
	r := sample()
	r.Counters.Superseded = 3
	if err := Check(r, 0, true); err == nil {
		t.Error("superseded inputs mean the server fell behind: --fail-on-errors should fail")
	}
}
