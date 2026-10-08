// Package metrics records what the bots measure: latency histograms that any number of goroutines can record into,
// and counters.
package metrics

import (
	"math"
	"sync"
	"time"
)

const (
	minValue    = time.Microsecond
	maxValue    = 60 * time.Second
	growth      = 1.01 // each bucket is 1% wider than the one before: percentiles within about 1%
	bucketCount = 1802 // ln(60 s / 1 µs) / ln(1.01), rounded up, + 1 for values below 1 µs
)

var logGrowth = math.Log(growth)

// Histogram counts durations in logarithmic buckets: constant memory whatever the number of samples, and
// percentiles within about 1%. The maximum and the mean are exact.
type Histogram struct {
	mu      sync.Mutex
	buckets [bucketCount]int64
	n       int64
	sum     time.Duration
	max     time.Duration
}

// Summary is a histogram's headline numbers. All zero when nothing was recorded.
type Summary struct {
	N    int64         `json:"n"`
	P50  time.Duration `json:"p50"`
	P95  time.Duration `json:"p95"`
	P99  time.Duration `json:"p99"`
	Max  time.Duration `json:"max"`
	Mean time.Duration `json:"mean"`
}

// NewHistogram returns an empty histogram.
func NewHistogram() *Histogram { return &Histogram{} }

func bucketOf(d time.Duration) int {
	if d < minValue {
		return 0
	}
	if d >= maxValue {
		return bucketCount - 1
	}
	i := 1 + int(math.Log(float64(d)/float64(minValue))/logGrowth)
	return min(i, bucketCount-1)
}

// valueOf is a bucket's representative value: the middle of its range.
func valueOf(i int) time.Duration {
	if i == 0 {
		return minValue / 2
	}
	lower := float64(minValue) * math.Pow(growth, float64(i-1))
	return time.Duration(lower * (1 + growth) / 2)
}

// Record adds one sample. Negative values count as zero; values over a minute go in the last bucket.
func (h *Histogram) Record(d time.Duration) {
	if d < 0 {
		d = 0
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	h.buckets[bucketOf(d)]++
	h.n++
	h.sum += d
	h.max = max(h.max, d)
}

// Reset forgets every sample.
func (h *Histogram) Reset() {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.buckets = [bucketCount]int64{} // field by field: the mutex is held, it must not be overwritten
	h.n, h.sum, h.max = 0, 0, 0
}

// Summary computes the percentiles.
func (h *Histogram) Summary() Summary {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.n == 0 {
		return Summary{}
	}
	percentile := func(p float64) time.Duration {
		rank := int64(math.Ceil(p * float64(h.n)))
		var seen int64
		for i, c := range h.buckets {
			seen += c
			if seen >= rank {
				return min(valueOf(i), h.max)
			}
		}
		return h.max
	}
	return Summary{
		N:    h.n,
		P50:  percentile(0.50),
		P95:  percentile(0.95),
		P99:  percentile(0.99),
		Max:  h.max,
		Mean: h.sum / time.Duration(h.n),
	}
}
