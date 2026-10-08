package store

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
)

// ErrNotFound: no such player.
var ErrNotFound = errors.New("not found")

// Leader is one line of a leaderboard.
type Leader struct {
	Name  string `json:"name"`
	Value int64  `json:"value"`
}

// Leaderboards are the top ten in each category. Empty lists, never null.
type Leaderboards struct {
	Kills       []Leader `json:"kills"`
	LongestLife []Leader `json:"longestLife"` // seconds
	PlayTime    []Leader `json:"playTime"`    // seconds
	Resources   []Leader `json:"resources"`   // items collected
	PvpKills    []Leader `json:"pvpKills"`
	Deepest     []Leader `json:"deepest"` // 1-3 caves, 4 sky
}

// ItemStat is an item's totals.
type ItemStat struct {
	Item      string `json:"item"`
	Icon      int    `json:"icon"`
	Collected int64  `json:"collected"`
	Crafted   int64  `json:"crafted"`
}

// MobStat is a mob kind's totals.
type MobStat struct {
	Mob           string `json:"mob"`
	Killed        int64  `json:"killed"`
	PlayersKilled int64  `json:"playersKilled"`
}

// TileStat is how often a tile was broken.
type TileStat struct {
	Tile   string `json:"tile"`
	Broken int64  `json:"broken"`
}

// HourPoint is one hour of activity.
type HourPoint struct {
	Hour  time.Time `json:"hour"`
	Joins int       `json:"joins"`
	Kills int       `json:"kills"`
}

// RecentEvent is a line of the activity feed.
type RecentEvent struct {
	At   time.Time `json:"at"`
	Text string    `json:"text"`
}

// PlayerCard is everything about one player.
type PlayerCard struct {
	Name           string     `json:"name"`
	FirstSeen      time.Time  `json:"firstSeen"`
	LastSeen       time.Time  `json:"lastSeen"`
	Sessions       int        `json:"sessions"`
	PlaySeconds    int64      `json:"playSeconds"`
	Kills          int        `json:"kills"`
	PvpKills       int        `json:"pvpKills"`
	Deaths         int        `json:"deaths"`
	LongestLife    int64      `json:"longestLife"`
	BossKills      int        `json:"bossKills"`
	DeepestLevel   int        `json:"deepestLevel"`
	ItemsCollected int64      `json:"itemsCollected"`
	Items          []ItemStat `json:"items"`
	Mobs           []MobStat  `json:"mobs"`
}

const leaderboardSize = 10

// Leaderboards reads the six top-ten lists.
func (s *Store) Leaderboards(ctx context.Context) (Leaderboards, error) {
	var b Leaderboards
	columns := []struct {
		column string
		into   *[]Leader
	}{
		{"kills", &b.Kills},
		{"longest_life_seconds", &b.LongestLife},
		{"play_seconds", &b.PlayTime},
		{"items_collected", &b.Resources},
		{"pvp_kills", &b.PvpKills},
		{"deepest_level", &b.Deepest},
	}
	for _, c := range columns {
		// The column names come from the fixed list above, never from a request.
		rows, err := s.pool.Query(ctx, fmt.Sprintf(
			`SELECT name, %[1]s::bigint FROM players WHERE %[1]s > 0 ORDER BY %[1]s DESC, name LIMIT %[2]d`,
			c.column, leaderboardSize))
		if err != nil {
			return b, err
		}
		leaders, err := pgx.CollectRows(rows, pgx.RowToStructByPos[Leader])
		if err != nil {
			return b, err
		}
		*c.into = nonNil(leaders)
	}
	return b, nil
}

// Items lists every item collected or crafted, most collected first.
func (s *Store) Items(ctx context.Context) ([]ItemStat, error) {
	rows, err := s.pool.Query(ctx, `SELECT item, icon, collected, crafted FROM item_totals
		ORDER BY collected DESC, crafted DESC, item`)
	if err != nil {
		return nil, err
	}
	items, err := pgx.CollectRows(rows, pgx.RowToStructByPos[ItemStat])
	return nonNil(items), err
}

// Mobs lists every mob kind, most killed first.
func (s *Store) Mobs(ctx context.Context) ([]MobStat, error) {
	rows, err := s.pool.Query(ctx, `SELECT mob, killed, players_killed FROM mob_totals ORDER BY killed DESC, mob`)
	if err != nil {
		return nil, err
	}
	mobs, err := pgx.CollectRows(rows, pgx.RowToStructByPos[MobStat])
	return nonNil(mobs), err
}

// Tiles lists every tile broken, most first.
func (s *Store) Tiles(ctx context.Context) ([]TileStat, error) {
	rows, err := s.pool.Query(ctx, `SELECT tile, broken FROM tile_totals ORDER BY broken DESC, tile`)
	if err != nil {
		return nil, err
	}
	tiles, err := pgx.CollectRows(rows, pgx.RowToStructByPos[TileStat])
	return nonNil(tiles), err
}

// Timeline counts joins and kills per hour over the last week (hours with neither are left out).
func (s *Store) Timeline(ctx context.Context) ([]HourPoint, error) {
	rows, err := s.pool.Query(ctx, `
		SELECT date_trunc('hour', at) AS hour,
		       count(*) FILTER (WHERE type = 'PlayerJoined')::int,
		       count(*) FILTER (WHERE type = 'MobKilled')::int
		FROM events
		WHERE at > now() - interval '7 days' AND type IN ('PlayerJoined', 'MobKilled')
		GROUP BY hour ORDER BY hour`)
	if err != nil {
		return nil, err
	}
	points, err := pgx.CollectRows(rows, pgx.RowToStructByPos[HourPoint])
	return nonNil(points), err
}

// Recent is the activity feed: the latest notable events, newest first, as sentences.
func (s *Store) Recent(ctx context.Context) ([]RecentEvent, error) {
	rows, err := s.pool.Query(ctx, `
		SELECT at, type, player, subject, killer_kind, count FROM events
		WHERE type IN ('PlayerJoined', 'PlayerKilled', 'BossDefeated', 'LevelReached')
		   OR (type = 'MobKilled' AND player <> '' AND (count >= 2 OR subject = 'Air Wizard'))
		ORDER BY at DESC, id DESC LIMIT 30`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	recent := []RecentEvent{}
	for rows.Next() {
		var at time.Time
		var kind, player, subject, killerKind string
		var count int
		if err := rows.Scan(&at, &kind, &player, &subject, &killerKind, &count); err != nil {
			return nil, err
		}
		recent = append(recent, RecentEvent{At: at, Text: sentence(kind, player, subject, killerKind, count)})
	}
	return recent, rows.Err()
}

func sentence(kind, player, subject, killerKind string, count int) string {
	switch kind {
	case "PlayerJoined":
		return player + " joined the game"
	case "MobKilled":
		if subject == "Air Wizard" {
			return player + " killed the Air Wizard"
		}
		return fmt.Sprintf("%s killed a level %d %s", player, count, subject)
	case "PlayerKilled":
		switch killerKind {
		case "player":
			return player + " was slain by " + subject
		case "mob":
			return player + " was killed by a " + subject
		default:
			return player + " died"
		}
	case "BossDefeated":
		if player == "" {
			return "The Air Wizard was defeated"
		}
		return player + " defeated the Air Wizard!"
	case "LevelReached":
		if subject == "" {
			return player + " reached a new level"
		}
		return player + " reached " + subject
	}
	return player
}

// Player reads one player's card, or ErrNotFound.
func (s *Store) Player(ctx context.Context, name string) (PlayerCard, error) {
	card := PlayerCard{Name: name}
	err := s.pool.QueryRow(ctx, `
		SELECT first_seen, last_seen, sessions, play_seconds, kills, pvp_kills, deaths, longest_life_seconds,
		       boss_kills, deepest_level, items_collected
		FROM players WHERE name = $1`, name).
		Scan(&card.FirstSeen, &card.LastSeen, &card.Sessions, &card.PlaySeconds, &card.Kills, &card.PvpKills,
			&card.Deaths, &card.LongestLife, &card.BossKills, &card.DeepestLevel, &card.ItemsCollected)
	if errors.Is(err, pgx.ErrNoRows) {
		return card, ErrNotFound
	}
	if err != nil {
		return card, err
	}
	rows, err := s.pool.Query(ctx, `SELECT item, icon, collected, 0::bigint FROM player_items WHERE player = $1
		ORDER BY collected DESC, item`, name)
	if err != nil {
		return card, err
	}
	if card.Items, err = pgx.CollectRows(rows, pgx.RowToStructByPos[ItemStat]); err != nil {
		return card, err
	}
	rows, err = s.pool.Query(ctx, `SELECT mob, killed::bigint, 0::bigint FROM player_mobs WHERE player = $1
		ORDER BY killed DESC, mob`, name)
	if err != nil {
		return card, err
	}
	if card.Mobs, err = pgx.CollectRows(rows, pgx.RowToStructByPos[MobStat]); err != nil {
		return card, err
	}
	card.Items, card.Mobs = nonNil(card.Items), nonNil(card.Mobs)
	return card, nil
}

// nonNil makes empty results encode as [] rather than null.
func nonNil[T any](list []T) []T {
	if list == nil {
		return []T{}
	}
	return list
}
