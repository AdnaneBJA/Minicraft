package procstat

import (
	"context"
	"os"
	"testing"
	"time"
)

func TestSamplerOwnProcess(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	s, err := StartSampler(ctx, int32(os.Getpid()), 200*time.Millisecond)
	if err != nil {
		t.Fatal(err)
	}
	time.Sleep(1200 * time.Millisecond)
	stats := s.Stats()
	if stats.Samples < 1 || stats.MaxRSS == 0 || stats.AvgRSS == 0 {
		t.Errorf("%+v", stats)
	}
}

func TestSamplerUnknownProcess(t *testing.T) {
	if _, err := StartSampler(context.Background(), 999_999_9, time.Second); err == nil {
		t.Error("want an error for a process that doesn't exist")
	}
}
