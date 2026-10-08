package store

import (
	"context"
)

// Summary is the dashboard's headline numbers.
type Summary struct {
	Online          int   `json:"online"`
	PlayersJoined   int64 `json:"playersJoined"`
	UniquePlayers   int64 `json:"uniquePlayers"`
	PlaySeconds     int64 `json:"playSeconds"`
	Worlds          int64 `json:"worlds"`
	CreaturesKilled int64 `json:"creaturesKilled"`
	Deaths          int64 `json:"deaths"`
	BossDefeats     int64 `json:"bossDefeats"`
	TreesChopped    int64 `json:"treesChopped"`
	RocksMined      int64 `json:"rocksMined"`
	ItemsCollected  int64 `json:"itemsCollected"`
	ChatLines       int64 `json:"chatLines"`
}

// Summary reads the totals. Online is filled in by the caller (the server keeps it in memory).
func (s *Store) Summary(ctx context.Context) (Summary, error) {
	var sum Summary
	err := s.pool.QueryRow(ctx, `
		SELECT
			COALESCE((SELECT value FROM counters WHERE name = 'players_joined'), 0),
			(SELECT count(*) FROM players WHERE sessions > 0),
			COALESCE((SELECT sum(play_seconds) FROM players), 0)::bigint,
			COALESCE((SELECT value FROM counters WHERE name = 'worlds'), 0),
			COALESCE((SELECT sum(killed) FROM mob_totals), 0)::bigint,
			COALESCE((SELECT sum(deaths) FROM players), 0)::bigint,
			COALESCE((SELECT value FROM counters WHERE name = 'boss_defeats'), 0),
			COALESCE((SELECT broken FROM tile_totals WHERE tile = 'Tree'), 0),
			COALESCE((SELECT sum(broken) FROM tile_totals WHERE tile IN ('Rock', 'Hard Rock', 'Iron Ore', 'Gold Ore', 'Gem Ore', 'Cloud Cactus')), 0)::bigint,
			COALESCE((SELECT sum(collected) FROM item_totals), 0)::bigint,
			COALESCE((SELECT value FROM counters WHERE name = 'chat'), 0)`).
		Scan(&sum.PlayersJoined, &sum.UniquePlayers, &sum.PlaySeconds, &sum.Worlds, &sum.CreaturesKilled, &sum.Deaths,
			&sum.BossDefeats, &sum.TreesChopped, &sum.RocksMined, &sum.ItemsCollected, &sum.ChatLines)
	return sum, err
}
