package web

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
	"github.com/AdnaneBJA/Minicraft/stats/internal/store"
	"github.com/AdnaneBJA/Minicraft/stats/internal/testdb"
)

func TestMain(m *testing.M) {
	code := m.Run()
	testdb.Stop()
	os.Exit(code)
}

// handler returns the public site over a fresh database holding `events`.
func handler(t *testing.T, events []ingest.Event) http.Handler {
	t.Helper()
	st, err := store.Open(context.Background(), testdb.New(t))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(st.Close)
	if _, err := st.Apply(context.Background(), events); err != nil {
		t.Fatal(err)
	}
	sprites := t.TempDir()
	if err := os.WriteFile(filepath.Join(sprites, "items.png"), []byte("\x89PNG fake"), 0o644); err != nil {
		t.Fatal(err)
	}
	return Handler(st, func() int { return 3 }, sprites)
}

func get(t *testing.T, h http.Handler, path string) *httptest.ResponseRecorder {
	t.Helper()
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

func getJSON(t *testing.T, h http.Handler, path string, into any) {
	t.Helper()
	rec := get(t, h, path)
	if rec.Code != http.StatusOK {
		t.Fatalf("%s: status %d: %s", path, rec.Code, rec.Body)
	}
	if err := json.Unmarshal(rec.Body.Bytes(), into); err != nil {
		t.Fatalf("%s: %v: %s", path, err, rec.Body)
	}
}

func ms(minutesAgo int) int64 {
	return time.Now().Add(-time.Duration(minutesAgo) * time.Minute).UnixMilli()
}

// A small world's worth of history.
func sample() []ingest.Event {
	return []ingest.Event{
		{ID: "w", Type: "WorldStarted", At: ms(60), Count: 1},
		{ID: "j1", Type: "PlayerJoined", At: ms(59), Player: "Alice", Count: 1},
		{ID: "j2", Type: "PlayerJoined", At: ms(58), Player: "Bob", Count: 1},
		{ID: "t1", Type: "TileBroken", At: ms(50), Player: "Alice", Subject: "Tree", Count: 1},
		{ID: "c1", Type: "ItemCollected", At: ms(50), Player: "Alice", Subject: "Wood", Count: 7, Icon: 0},
		{ID: "c2", Type: "ItemCollected", At: ms(49), Player: "Bob", Subject: "Stone", Count: 4, Icon: 1},
		{ID: "k1", Type: "MobKilled", At: ms(40), Player: "Alice", Subject: "Zombie", Count: 3},
		{ID: "k2", Type: "MobKilled", At: ms(39), Player: "Bob", Subject: "Zombie", Count: 1},
		{ID: "k3", Type: "MobKilled", At: ms(38), Player: "Alice", Subject: "Slime", Count: 1},
		{ID: "d1", Type: "PlayerKilled", At: ms(30), Player: "Bob", Subject: "Skeleton", KillerKind: "mob", Count: 420},
		{ID: "l1", Type: "LevelReached", At: ms(20), Player: "Alice", Subject: "Cave 1", Count: 2},
		{ID: "x1", Type: "PlayerLeft", At: ms(10), Player: "Bob", Count: 3000},
	}
}

func TestSummary(t *testing.T) {
	h := handler(t, sample())
	var s store.Summary
	getJSON(t, h, "/stats/api/summary", &s)
	if s.Online != 3 || s.PlayersJoined != 2 || s.UniquePlayers != 2 || s.CreaturesKilled != 3 || s.Deaths != 1 ||
		s.TreesChopped != 1 || s.ItemsCollected != 11 || s.Worlds != 1 || s.PlaySeconds != 3000 {
		t.Errorf("summary %+v", s)
	}
}

func TestLeaderboards(t *testing.T) {
	h := handler(t, sample())
	var boards store.Leaderboards
	getJSON(t, h, "/stats/api/leaderboards", &boards)
	if len(boards.Kills) != 2 || boards.Kills[0].Name != "Alice" || boards.Kills[0].Value != 2 {
		t.Errorf("kills %+v", boards.Kills)
	}
	if len(boards.LongestLife) != 1 || boards.LongestLife[0].Name != "Bob" || boards.LongestLife[0].Value != 420 {
		t.Errorf("longest life %+v", boards.LongestLife)
	}
	if len(boards.Resources) != 2 || boards.Resources[0].Name != "Alice" {
		t.Errorf("resources %+v", boards.Resources)
	}
	if len(boards.Deepest) != 1 || boards.Deepest[0].Value != 1 {
		t.Errorf("deepest %+v", boards.Deepest)
	}
}

func TestLeaderboardsShowTen(t *testing.T) {
	var events []ingest.Event
	for i := 0; i < 15; i++ {
		name := "P" + string(rune('a'+i))
		for k := 0; k <= i; k++ {
			events = append(events, ingest.Event{ID: name + "-" + string(rune('a'+k)), Type: "MobKilled", At: ms(5),
				Player: name, Subject: "Cow", Count: 1})
		}
	}
	h := handler(t, events)
	var boards store.Leaderboards
	getJSON(t, h, "/stats/api/leaderboards", &boards)
	if len(boards.Kills) != 10 || boards.Kills[0].Name != "Po" || boards.Kills[0].Value != 15 {
		t.Errorf("kills %+v", boards.Kills)
	}
}

func TestBreakdowns(t *testing.T) {
	h := handler(t, sample())
	var items []store.ItemStat
	getJSON(t, h, "/stats/api/items", &items)
	if len(items) != 2 || items[0].Item != "Wood" || items[0].Collected != 7 {
		t.Errorf("items %+v", items)
	}
	var mobs []store.MobStat
	getJSON(t, h, "/stats/api/mobs", &mobs)
	if len(mobs) < 2 || mobs[0].Mob != "Zombie" || mobs[0].Killed != 2 {
		t.Errorf("mobs %+v", mobs)
	}
	var tiles []store.TileStat
	getJSON(t, h, "/stats/api/tiles", &tiles)
	if len(tiles) != 1 || tiles[0].Tile != "Tree" {
		t.Errorf("tiles %+v", tiles)
	}
	var timeline []store.HourPoint
	getJSON(t, h, "/stats/api/timeline", &timeline)
	joins, kills := 0, 0
	for _, p := range timeline {
		joins += p.Joins
		kills += p.Kills
	}
	if joins != 2 || kills != 3 {
		t.Errorf("timeline joins %d kills %d", joins, kills)
	}
	var recent []store.RecentEvent
	getJSON(t, h, "/stats/api/recent", &recent)
	if len(recent) == 0 || recent[0].Text != "Alice reached Cave 1" {
		t.Errorf("recent, newest first: %+v", recent)
	}
	found := false
	for _, r := range recent {
		found = found || r.Text == "Bob was killed by a Skeleton"
	}
	if !found {
		t.Errorf("recent misses Bob's death: %+v", recent)
	}
}

func TestPlayerCard(t *testing.T) {
	h := handler(t, sample())
	var card store.PlayerCard
	getJSON(t, h, "/stats/api/player/Alice", &card)
	if card.Name != "Alice" || card.Kills != 2 || card.DeepestLevel != 1 || len(card.Items) != 1 || len(card.Mobs) != 2 {
		t.Errorf("card %+v", card)
	}
	if rec := get(t, h, "/stats/api/player/Nobody"); rec.Code != http.StatusNotFound {
		t.Errorf("unknown player: %d", rec.Code)
	}
	page := get(t, h, "/stats/player/Alice")
	if page.Code != http.StatusOK || !strings.Contains(page.Body.String(), "Alice") {
		t.Errorf("player page %d", page.Code)
	}
	if rec := get(t, h, "/stats/player/Nobody"); rec.Code != http.StatusNotFound {
		t.Errorf("unknown player page: %d", rec.Code)
	}
}

func TestDashboard(t *testing.T) {
	h := handler(t, sample())
	rec := get(t, h, "/stats")
	body := rec.Body.String()
	if rec.Code != http.StatusOK || !strings.Contains(body, "Players joined") || !strings.Contains(body, "Alice") {
		t.Fatalf("dashboard %d: %.300s", rec.Code, body)
	}
	if get(t, h, "/stats/").Code != http.StatusOK {
		t.Error("trailing slash")
	}
}

func TestDashboardEmptyDatabase(t *testing.T) {
	h := handler(t, nil)
	for _, path := range []string{"/stats/api/summary", "/stats/api/leaderboards", "/stats/api/items", "/stats/api/mobs",
		"/stats/api/tiles", "/stats/api/timeline", "/stats/api/recent"} {
		rec := get(t, h, path)
		if rec.Code != http.StatusOK || strings.Contains(rec.Body.String(), "null") {
			t.Errorf("%s: %d %s", path, rec.Code, rec.Body)
		}
	}
	rec := get(t, h, "/stats")
	if rec.Code != http.StatusOK || !strings.Contains(rec.Body.String(), "Players joined") {
		t.Errorf("empty dashboard %d", rec.Code)
	}
}

func TestStaticAndSprites(t *testing.T) {
	h := handler(t, nil)
	if rec := get(t, h, "/stats/static/app.js"); rec.Code != http.StatusOK {
		t.Errorf("app.js %d", rec.Code)
	}
	if rec := get(t, h, "/stats/sprites/items.png"); rec.Code != http.StatusOK {
		t.Errorf("sprite %d", rec.Code)
	}
	for _, path := range []string{"/stats/sprites/", "/stats/sprites/../go.mod", "/stats/sprites/notes.txt"} {
		if rec := get(t, h, path); rec.Code == http.StatusOK {
			t.Errorf("%s should not be served", path)
		}
	}
}

func TestPlayerNamesAreEscaped(t *testing.T) {
	// Names are validated on the way in; the pages escape anyway.
	h := handler(t, []ingest.Event{{ID: "1", Type: "PlayerJoined", At: ms(1), Player: "Al_ice-1", Count: 1}})
	if body := get(t, h, "/stats/player/%3Cscript%3E").Body.String(); strings.Contains(body, "<script>alert") {
		t.Error("unescaped")
	}
}
