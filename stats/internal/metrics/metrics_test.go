package metrics

import (
	"strings"
	"testing"
	"time"

	"github.com/prometheus/client_golang/prometheus/testutil"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
)

func TestReportSetsGauges(t *testing.T) {
	m := New()
	m.Report(3, &ingest.Health{At: 1000, Connections: 4, Observers: 1, Ticks: 600, HistoryTicks: 500,
		HistoryBytes: 9000, Backlog: 2, RSSBytes: 4096, CPUSeconds: 1.5}, time.Unix(50, 0))
	want := `
# HELP minicraft_connections Open WebSocket connections to the game server.
# TYPE minicraft_connections gauge
minicraft_connections 4
# HELP minicraft_players_online Players in the world.
# TYPE minicraft_players_online gauge
minicraft_players_online 3
`
	if err := testutil.GatherAndCompare(m.Registry(), strings.NewReader(want),
		"minicraft_connections", "minicraft_players_online"); err != nil {
		t.Error(err)
	}
	if got := testutil.ToFloat64(m.lastReport); got != 50 {
		t.Errorf("last report %v", got)
	}
}

func TestOlderHealthDoesNotOverwrite(t *testing.T) {
	m := New()
	m.Report(5, &ingest.Health{At: 2000, Connections: 5}, time.Unix(10, 0))
	m.Report(1, &ingest.Health{At: 1000, Connections: 1}, time.Unix(11, 0)) // a retried old batch
	if got := testutil.ToFloat64(m.connections); got != 5 {
		t.Errorf("connections %v, want 5", got)
	}
	if got := testutil.ToFloat64(m.online); got != 5 {
		t.Errorf("online %v, want 5", got)
	}
	if got := testutil.ToFloat64(m.lastReport); got != 11 {
		t.Errorf("last report %v: an old batch still shows the game server is alive", got)
	}
}

func TestBatchWithoutHealthKeepsGauges(t *testing.T) {
	m := New()
	m.Report(2, &ingest.Health{At: 1000, Connections: 2}, time.Unix(10, 0))
	m.Report(9, nil, time.Unix(11, 0))
	if got := testutil.ToFloat64(m.connections); got != 2 {
		t.Errorf("connections %v", got)
	}
	if got := testutil.ToFloat64(m.online); got != 9 {
		t.Errorf("online %v: without health, online still comes from the batch", got)
	}
}

func TestCountsBatchesAndEvents(t *testing.T) {
	m := New()
	m.Batch("accepted")
	m.Batch("invalid")
	m.Events([]ingest.Event{{Type: ingest.TileBroken}, {Type: ingest.TileBroken}, {Type: ingest.ChatSent}})
	m.DBWrite(3 * time.Millisecond)
	if got := testutil.ToFloat64(m.batches.WithLabelValues("accepted")); got != 1 {
		t.Errorf("accepted %v", got)
	}
	if got := testutil.ToFloat64(m.events.WithLabelValues(ingest.TileBroken)); got != 2 {
		t.Errorf("TileBroken %v", got)
	}
	if n := testutil.CollectAndCount(m.dbWrite); n != 1 {
		t.Errorf("db write series %d", n)
	}
}
