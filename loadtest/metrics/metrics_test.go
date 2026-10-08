package metrics

import (
	"math"
	"sync"
	"testing"
	"time"
)

func near(t *testing.T, what string, got, want time.Duration, tolerance float64) {
	t.Helper()
	if math.Abs(float64(got-want)) > tolerance*float64(want) {
		t.Errorf("%s = %v, want %v ±%.0f%%", what, got, want, tolerance*100)
	}
}

func TestPercentilesUniform(t *testing.T) {
	h := NewHistogram()
	for i := 1; i <= 10000; i++ {
		h.Record(time.Duration(i) * time.Microsecond)
	}
	s := h.Summary()
	if s.N != 10000 {
		t.Fatalf("n = %d", s.N)
	}
	near(t, "p50", s.P50, 5000*time.Microsecond, 0.02)
	near(t, "p95", s.P95, 9500*time.Microsecond, 0.02)
	near(t, "p99", s.P99, 9900*time.Microsecond, 0.02)
	if s.Max != 10000*time.Microsecond {
		t.Errorf("max = %v", s.Max)
	}
	near(t, "mean", s.Mean, 5000*time.Microsecond, 0.01)
}

func TestPercentilesSkewed(t *testing.T) {
	h := NewHistogram()
	for i := 0; i < 99; i++ {
		h.Record(time.Millisecond)
	}
	h.Record(time.Second)
	s := h.Summary()
	near(t, "p99", s.P99, time.Millisecond, 0.02)
	if s.Max != time.Second {
		t.Errorf("max = %v", s.Max)
	}
}

func TestReportEmptyHistograms(t *testing.T) {
	s := NewHistogram().Summary()
	if s != (Summary{}) {
		t.Errorf("empty summary %+v", s)
	}
}

func TestOutOfRangeValuesAreClamped(t *testing.T) {
	h := NewHistogram()
	h.Record(-5 * time.Millisecond)
	h.Record(10 * time.Minute)
	s := h.Summary()
	if s.N != 2 || s.Max != 10*time.Minute {
		t.Errorf("%+v", s)
	}
}

func TestConcurrentRecord(t *testing.T) {
	h := NewHistogram()
	var wg sync.WaitGroup
	for g := 0; g < 16; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < 10000; i++ {
				h.Record(time.Duration(i) * time.Microsecond)
				_ = h.Summary().N
			}
		}()
	}
	wg.Wait()
	if n := h.Summary().N; n != 160000 {
		t.Errorf("n = %d", n)
	}
}

func TestResetStartsOver(t *testing.T) {
	h := NewHistogram()
	h.Record(time.Second)
	h.Reset()
	if h.Summary().N != 0 {
		t.Error("not reset")
	}
}
