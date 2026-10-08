package store

import (
	"context"
	"os"
	"testing"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
	"github.com/AdnaneBJA/Minicraft/stats/internal/testdb"
)

func TestMain(m *testing.M) {
	code := m.Run()
	testdb.Stop()
	os.Exit(code)
}

func open(t *testing.T) *Store {
	t.Helper()
	st, err := Open(context.Background(), testdb.New(t))
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	t.Cleanup(st.Close)
	return st
}

func scalar(t *testing.T, st *Store, query string, args ...any) int64 {
	t.Helper()
	var v int64
	if err := st.pool.QueryRow(context.Background(), query, args...).Scan(&v); err != nil {
		t.Fatalf("%s: %v", query, err)
	}
	return v
}

func TestApplyIsIdempotent(t *testing.T) {
	st := open(t)
	ctx := context.Background()
	events := []ingest.Event{
		{ID: "1-1-0", Type: "PlayerJoined", At: 1000, Player: "Alice", Count: 1},
		{ID: "1-2-0", Type: "ItemCollected", At: 2000, Player: "Alice", Subject: "Wood", Count: 3, Icon: 0},
		{ID: "1-3-0", Type: "ItemCollected", At: 3000, Player: "Alice", Subject: "Wood", Count: 2, Icon: 0},
	}
	applied, err := st.Apply(ctx, events)
	if err != nil || applied != 3 {
		t.Fatalf("first apply: %d, %v", applied, err)
	}
	applied, err = st.Apply(ctx, events)
	if err != nil || applied != 0 {
		t.Fatalf("second apply: %d, %v", applied, err)
	}
	if got := scalar(t, st, "SELECT collected FROM item_totals WHERE item = 'Wood'"); got != 5 {
		t.Errorf("wood collected = %d, want 5", got)
	}
	if got := scalar(t, st, "SELECT value FROM counters WHERE name = 'players_joined'"); got != 1 {
		t.Errorf("players joined = %d, want 1", got)
	}
}

func TestTallies(t *testing.T) {
	st := open(t)
	ctx := context.Background()
	events := []ingest.Event{
		{ID: "a", Type: "WorldStarted", At: 1, Count: 1},
		{ID: "b", Type: "PlayerJoined", At: 2, Player: "Alice", Count: 1},
		{ID: "c", Type: "PlayerJoined", At: 3, Player: "Bob", Count: 1},
		{ID: "d", Type: "TileBroken", At: 4, Player: "Alice", Subject: "Tree", Count: 1},
		{ID: "e", Type: "ItemCrafted", At: 5, Player: "Alice", Subject: "Workbench", Count: 1, Icon: 2},
		{ID: "f", Type: "MobKilled", At: 6, Player: "Alice", Subject: "Zombie", Count: 2},
		{ID: "g", Type: "MobKilled", At: 7, Subject: "Cow", Count: 1},
		{ID: "h", Type: "PlayerKilled", At: 8, Player: "Bob", Subject: "Alice", KillerKind: "player", Count: 90},
		{ID: "i", Type: "PlayerKilled", At: 9, Player: "Alice", Subject: "Skeleton", KillerKind: "mob", Count: 300},
		{ID: "j", Type: "PlayerKilled", At: 10, Player: "Alice", KillerKind: "environment", Count: 12},
		{ID: "k", Type: "LevelReached", At: 11, Player: "Alice", Subject: "Cave 2", Count: 3},
		{ID: "l", Type: "LevelReached", At: 12, Player: "Alice", Subject: "Cave 1", Count: 2},
		{ID: "m", Type: "BossDefeated", At: 13, Player: "Alice", Subject: "Air Wizard", Count: 1},
		{ID: "n", Type: "ChatSent", At: 14, Player: "Bob", Count: 1},
		{ID: "o", Type: "PlayerLeft", At: 15, Player: "Bob", Count: 600},
	}
	if _, err := st.Apply(ctx, events); err != nil {
		t.Fatal(err)
	}
	checks := []struct {
		query string
		want  int64
	}{
		{"SELECT value FROM counters WHERE name = 'worlds'", 1},
		{"SELECT value FROM counters WHERE name = 'players_joined'", 2},
		{"SELECT value FROM counters WHERE name = 'chat'", 1},
		{"SELECT value FROM counters WHERE name = 'boss_defeats'", 1},
		{"SELECT broken FROM tile_totals WHERE tile = 'Tree'", 1},
		{"SELECT crafted FROM item_totals WHERE item = 'Workbench'", 1},
		{"SELECT killed FROM mob_totals WHERE mob = 'Zombie'", 1},
		{"SELECT killed FROM mob_totals WHERE mob = 'Cow'", 1},
		{"SELECT players_killed FROM mob_totals WHERE mob = 'Skeleton'", 1},
		{"SELECT kills FROM players WHERE name = 'Alice'", 1},
		{"SELECT pvp_kills FROM players WHERE name = 'Alice'", 1},
		{"SELECT deaths FROM players WHERE name = 'Alice'", 2},
		{"SELECT deaths FROM players WHERE name = 'Bob'", 1},
		{"SELECT longest_life_seconds FROM players WHERE name = 'Alice'", 300},
		{"SELECT boss_kills FROM players WHERE name = 'Alice'", 1},
		{"SELECT deepest_level FROM players WHERE name = 'Alice'", 2},
		{"SELECT play_seconds FROM players WHERE name = 'Bob'", 600},
		{"SELECT sessions FROM players WHERE name = 'Bob'", 1},
		{"SELECT count FROM kills WHERE killer = 'Alice' AND victim = 'Bob'", 1},
		{"SELECT killed FROM player_mobs WHERE player = 'Alice' AND mob = 'Zombie'", 1},
	}
	for _, c := range checks {
		if got := scalar(t, st, c.query); got != c.want {
			t.Errorf("%s = %d, want %d", c.query, got, c.want)
		}
	}
}

func TestDeepestLevelCountsTheSkyAsDeepest(t *testing.T) {
	st := open(t)
	ctx := context.Background()
	if _, err := st.Apply(ctx, []ingest.Event{
		{ID: "1", Type: "LevelReached", At: 1, Player: "Alice", Count: 4}, // the deepest cave
		{ID: "2", Type: "LevelReached", At: 2, Player: "Alice", Count: 0}, // the sky, beyond it
	}); err != nil {
		t.Fatal(err)
	}
	if got := scalar(t, st, "SELECT deepest_level FROM players WHERE name = 'Alice'"); got != 4 {
		t.Errorf("deepest = %d, want 4 (the sky)", got)
	}
}

func TestLongestLifeKeepsMax(t *testing.T) {
	st := open(t)
	ctx := context.Background()
	for i, seconds := range []int{50, 400, 120} {
		if _, err := st.Apply(ctx, []ingest.Event{{ID: string(rune('a' + i)), Type: "PlayerKilled", At: 1,
			Player: "Alice", KillerKind: "environment", Count: seconds}}); err != nil {
			t.Fatal(err)
		}
	}
	if got := scalar(t, st, "SELECT longest_life_seconds FROM players WHERE name = 'Alice'"); got != 400 {
		t.Errorf("longest life = %d, want 400", got)
	}
}

func TestApplyFailsWhenClosed(t *testing.T) {
	st := open(t)
	st.Close()
	if _, err := st.Apply(context.Background(), []ingest.Event{{ID: "1", Type: "ChatSent", Player: "A"}}); err == nil {
		t.Fatal("want an error from a closed store")
	}
}

func TestSelfKillIsNotPvp(t *testing.T) {
	st := open(t)
	if _, err := st.Apply(context.Background(), []ingest.Event{{ID: "1", Type: "PlayerKilled", At: 1, Player: "Alice",
		Subject: "Alice", KillerKind: "player", Count: 10}}); err != nil {
		t.Fatal(err)
	}
	if got := scalar(t, st, "SELECT pvp_kills FROM players WHERE name = 'Alice'"); got != 0 {
		t.Errorf("pvp kills = %d, want 0", got)
	}
	if got := scalar(t, st, "SELECT count(*) FROM kills"); got != 0 {
		t.Errorf("kills rows = %d, want 0", got)
	}
	if got := scalar(t, st, "SELECT deaths FROM players WHERE name = 'Alice'"); got != 1 {
		t.Errorf("deaths = %d, want 1 (the death still counts)", got)
	}
}
