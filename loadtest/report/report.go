// Package report turns a load test's measurements into the live line, report.json and report.md.
package report

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
	"github.com/AdnaneBJA/Minicraft/loadtest/procstat"
)

// Config is how the run was set up (what the report needs to say).
type Config struct {
	URL      string        `json:"url"`
	Mode     string        `json:"mode"`
	Bots     int           `json:"bots"`
	Ramp     time.Duration `json:"ramp"`
	Duration time.Duration `json:"duration"`
	Step     int           `json:"step,omitempty"`
	Every    time.Duration `json:"every,omitempty"`
	MaxP99   time.Duration `json:"maxP99,omitempty"`
}

// Step is one stage of a ramp: the bots online and how that stage went.
type Step struct {
	Bots       int             `json:"bots"`
	Latency    metrics.Summary `json:"latency"`
	Refused    int64           `json:"refused"`
	Dropped    int64           `json:"dropped"`
	JoinFailed int64           `json:"joinFailed"`
}

// Break is where a ramp stopped working, and why.
type Break struct {
	Bots  int    `json:"bots"`
	Cause string `json:"cause"`
}

// Probe is one soak-mode join: how long a fresh player waited for the world, and how long its history was.
type Probe struct {
	At         time.Duration `json:"at"` // since the start
	Join       time.Duration `json:"join"`
	HistoryLen int           `json:"historyLen"`
}

// Result is a whole run.
type Result struct {
	Config    Config                   `json:"config"`
	StartedAt time.Time                `json:"startedAt"`
	Duration  time.Duration            `json:"duration"`
	Cancelled bool                     `json:"cancelled"`
	Latency   metrics.Summary          `json:"inputLatency"`
	Jitter    metrics.Summary          `json:"tickJitter"`
	Join      metrics.Summary          `json:"joinTime"`
	Counters  metrics.CountersSnapshot `json:"counters"`
	Server    *procstat.ProcStats      `json:"server,omitempty"`
	Steps     []Step                   `json:"steps,omitempty"`
	Break     *Break                   `json:"breakingPoint,omitempty"`
	Probes    []Probe                  `json:"joinProbes,omitempty"`
}

// ms formats a duration for people: "48 ms", "4.2 ms", "0.40 ms".
func ms(d time.Duration) string {
	switch {
	case d >= 10*time.Millisecond:
		return fmt.Sprintf("%d ms", d.Round(time.Millisecond).Milliseconds())
	case d >= time.Millisecond:
		return fmt.Sprintf("%.1f ms", float64(d)/float64(time.Millisecond))
	default:
		return fmt.Sprintf("%.2f ms", float64(d)/float64(time.Millisecond))
	}
}

// compactMs is ms for the live line: "48ms", "4ms", "0.3ms".
func compactMs(d time.Duration) string {
	if d < time.Millisecond {
		return fmt.Sprintf("%.1fms", float64(d)/float64(time.Millisecond))
	}
	return fmt.Sprintf("%dms", d.Round(time.Millisecond).Milliseconds())
}

// rate formats a byte count: "5.9 KB", "1.8 MB" (binary units).
func rate(bytes float64) string {
	switch {
	case bytes >= 1<<20:
		return fmt.Sprintf("%.1f MB", bytes/(1<<20))
	case bytes >= 1<<10:
		return fmt.Sprintf("%.1f KB", bytes/(1<<10))
	default:
		return fmt.Sprintf("%.0f B", bytes)
	}
}

// LiveLine is the once-a-second progress line.
func LiveLine(elapsed time.Duration, bots, target int, latency, jitter metrics.Summary, bytesPerSecond, drops int64) string {
	secs := int(elapsed.Seconds())
	return fmt.Sprintf("t=%02d:%02d bots=%d/%d lat p50=%s p99=%s jitter p99=%s rx=%s/s drops=%d",
		secs/60, secs%60, bots, target, compactMs(latency.P50), compactMs(latency.P99), compactMs(jitter.P99),
		rate(float64(bytesPerSecond)), drops)
}

// JSON is the result as indented JSON (durations in nanoseconds).
func JSON(r Result) ([]byte, error) { return json.MarshalIndent(r, "", "  ") }

func summaryRow(name string, s metrics.Summary) string {
	if s.N == 0 {
		return fmt.Sprintf("| %s | – | – | – | – | 0 |\n", name)
	}
	return fmt.Sprintf("| %s | %s | %s | %s | %s | %d |\n", name, ms(s.P50), ms(s.P95), ms(s.P99), ms(s.Max), s.N)
}

// Markdown is the report for people (and for pasting into the README).
func Markdown(r Result) string {
	var b strings.Builder
	fmt.Fprintf(&b, "# Load test: %d bots, %s mode\n\n", r.Config.Bots, r.Config.Mode)
	fmt.Fprintf(&b, "Target `%s`, ran %s", r.Config.URL, r.Duration.Round(time.Second))
	if !r.StartedAt.IsZero() {
		fmt.Fprintf(&b, " from %s", r.StartedAt.UTC().Format("2006-01-02 15:04 UTC"))
	}
	if r.Cancelled {
		b.WriteString(" (stopped early)")
	}
	b.WriteString(".\n\n")

	b.WriteString("| | p50 | p95 | p99 | max | samples |\n|---|---|---|---|---|---|\n")
	b.WriteString(summaryRow("Input latency", r.Latency))
	b.WriteString(summaryRow("Tick jitter", r.Jitter))
	b.WriteString(summaryRow("Join time", r.Join))
	b.WriteString("\n*Input latency: a key change sent → the first tick that carries it. Tick jitter: how far the gap " +
		"between ticks strays from 16.7 ms.*\n\n")

	c := r.Counters
	secs := max(r.Duration.Seconds(), 1)
	b.WriteString("| Joins | Join refusals | Connections refused | Drops | Messages in | Data in | Superseded inputs |\n")
	b.WriteString("|---|---|---|---|---|---|---|\n")
	fmt.Fprintf(&b, "| %d | %d | %d | %d | %d (%.0f/s) | %s (%s/s) | %d |\n\n", c.Joined, c.JoinFailed, c.Refused,
		c.Dropped, c.Messages, float64(c.Messages)/secs, rate(float64(c.Bytes)), rate(float64(c.Bytes)/secs),
		c.Superseded)

	if s := r.Server; s != nil && s.Samples > 0 {
		fmt.Fprintf(&b, "**Server process:** CPU avg %.1f%%, max %.1f%% of one core; memory avg %s, max %s (%d samples).\n\n",
			s.AvgCPU, s.MaxCPU, rate(float64(s.AvgRSS)), rate(float64(s.MaxRSS)), s.Samples)
	}

	if len(r.Steps) > 0 {
		b.WriteString("## Ramp\n\n| Bots | Input latency p50 | p99 | Refused | Dropped | Join refusals |\n|---|---|---|---|---|---|\n")
		for _, s := range r.Steps {
			p50, p99 := "–", "–"
			if s.Latency.N > 0 {
				p50, p99 = ms(s.Latency.P50), ms(s.Latency.P99)
			}
			fmt.Fprintf(&b, "| %d | %s | %s | %d | %d | %d |\n", s.Bots, p50, p99, s.Refused, s.Dropped, s.JoinFailed)
		}
		b.WriteString("\n")
		if r.Break != nil {
			fmt.Fprintf(&b, "**Breaking point:** %d bots (%s).\n\n", r.Break.Bots, r.Break.Cause)
		} else {
			fmt.Fprintf(&b, "**No breaking point** up to %d bots.\n\n", r.Config.Bots)
		}
	}

	if len(r.Probes) > 0 {
		b.WriteString("## Joining an aging world\n\n| After | History (ticks) | Join time |\n|---|---|---|\n")
		for _, p := range r.Probes {
			fmt.Fprintf(&b, "| %s | %d | %s |\n", p.At.Round(time.Second), p.HistoryLen, ms(p.Join))
		}
		b.WriteString("\n")
	}
	return b.String()
}

// WriteFiles writes base.json and base.md.
func WriteFiles(r Result, base string) error {
	data, err := JSON(r)
	if err != nil {
		return err
	}
	if err := os.WriteFile(base+".json", data, 0o644); err != nil {
		return err
	}
	return os.WriteFile(base+".md", []byte(Markdown(r)), 0o644)
}

// Check fails a run (for CI) when p99 input latency reaches maxP99 (0: no limit), or, with failOnErrors, when any
// join was refused or any bot dropped or couldn't connect.
func Check(r Result, maxP99 time.Duration, failOnErrors bool) error {
	var problems []string
	if maxP99 > 0 {
		switch {
		case r.Latency.N == 0:
			problems = append(problems, "no input latency was measured")
		case r.Latency.P99 >= maxP99:
			problems = append(problems, fmt.Sprintf("p99 input latency %s ≥ %s", ms(r.Latency.P99), ms(maxP99)))
		}
	}
	if failOnErrors {
		c := r.Counters
		if c.JoinFailed+c.Refused+c.Dropped > 0 {
			problems = append(problems, fmt.Sprintf("%d join refusals, %d connections refused, %d drops",
				c.JoinFailed, c.Refused, c.Dropped))
		}
		if c.Superseded > 0 {
			problems = append(problems, fmt.Sprintf("%d inputs were overtaken before the server showed them "+
				"(the server fell behind)", c.Superseded))
		}
	}
	if len(problems) > 0 {
		return errors.New(strings.Join(problems, "; "))
	}
	return nil
}
