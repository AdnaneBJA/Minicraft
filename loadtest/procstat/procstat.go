// Package procstat samples a process's CPU and memory while a load test runs (the server, when it runs locally).
package procstat

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/shirou/gopsutil/v4/process"
)

// ProcStats is what the sampling saw. CPU is in percent of one core.
type ProcStats struct {
	Samples int     `json:"samples"`
	MaxCPU  float64 `json:"maxCpu"`
	AvgCPU  float64 `json:"avgCpu"`
	MaxRSS  uint64  `json:"maxRss"`
	AvgRSS  uint64  `json:"avgRss"`
}

// Sampler samples one process until its context ends.
type Sampler struct {
	mu     sync.Mutex
	stats  ProcStats
	cpuSum float64
	rssSum uint64
}

// StartSampler starts sampling process `pid` every `every`. Error if there's no such process.
func StartSampler(ctx context.Context, pid int32, every time.Duration) (*Sampler, error) {
	p, err := process.NewProcessWithContext(ctx, pid)
	if err != nil {
		return nil, fmt.Errorf("process %d: %w", pid, err)
	}
	if _, err := p.PercentWithContext(ctx, 0); err != nil { // the first call only sets the baseline
		return nil, fmt.Errorf("process %d: %w", pid, err)
	}
	s := &Sampler{}
	go func() {
		ticker := time.NewTicker(every)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				cpu, err := p.PercentWithContext(ctx, 0)
				if err != nil {
					continue
				}
				mem, err := p.MemoryInfoWithContext(ctx)
				if err != nil {
					continue
				}
				s.add(cpu, mem.RSS)
			}
		}
	}()
	return s, nil
}

func (s *Sampler) add(cpu float64, rss uint64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.stats.Samples++
	s.cpuSum += cpu
	s.rssSum += rss
	s.stats.MaxCPU = max(s.stats.MaxCPU, cpu)
	s.stats.MaxRSS = max(s.stats.MaxRSS, rss)
	s.stats.AvgCPU = s.cpuSum / float64(s.stats.Samples)
	s.stats.AvgRSS = s.rssSum / uint64(s.stats.Samples)
}

// Stats is what was sampled so far.
func (s *Sampler) Stats() ProcStats {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.stats
}
