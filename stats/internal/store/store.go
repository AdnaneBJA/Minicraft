// Package store keeps the stats in PostgreSQL: every event once, and the running totals the dashboard reads.
package store

import (
	"context"
	"embed"
	"fmt"
	"io/fs"
	"sort"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
)

//go:embed migrations/*.sql
var migrations embed.FS

// Store is the stats database.
type Store struct {
	pool *pgxpool.Pool
}

// Open connects and brings the schema up to date.
func Open(ctx context.Context, url string) (*Store, error) {
	pool, err := pgxpool.New(ctx, url)
	if err != nil {
		return nil, err
	}
	st := &Store{pool: pool}
	if err := st.migrate(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("migrate: %w", err)
	}
	return st, nil
}

// Close releases the connections. Later calls fail.
func (s *Store) Close() { s.pool.Close() }

// Ping checks the database answers.
func (s *Store) Ping(ctx context.Context) error { return s.pool.Ping(ctx) }

// migrate applies, in order, the migrations not applied yet.
func (s *Store) migrate(ctx context.Context) error {
	if _, err := s.pool.Exec(ctx, `CREATE TABLE IF NOT EXISTS schema_migrations (name text PRIMARY KEY)`); err != nil {
		return err
	}
	names, err := fs.Glob(migrations, "migrations/*.sql")
	if err != nil {
		return err
	}
	sort.Strings(names)
	for _, name := range names {
		var done bool
		if err := s.pool.QueryRow(ctx, `SELECT EXISTS (SELECT 1 FROM schema_migrations WHERE name = $1)`, name).
			Scan(&done); err != nil {
			return err
		}
		if done {
			continue
		}
		sql, err := migrations.ReadFile(name)
		if err != nil {
			return err
		}
		err = pgx.BeginFunc(ctx, s.pool, func(tx pgx.Tx) error {
			if _, err := tx.Exec(ctx, string(sql)); err != nil {
				return err
			}
			_, err := tx.Exec(ctx, `INSERT INTO schema_migrations (name) VALUES ($1)`, name)
			return err
		})
		if err != nil {
			return fmt.Errorf("%s: %w", name, err)
		}
	}
	return nil
}

// Apply stores a batch in one transaction and returns how many events were new. An event already stored (a
// retried batch) is skipped, totals included, so everything counts exactly once.
func (s *Store) Apply(ctx context.Context, events []ingest.Event) (int, error) {
	applied := 0
	err := pgx.BeginFunc(ctx, s.pool, func(tx pgx.Tx) error {
		applied = 0
		for _, e := range events {
			at := time.UnixMilli(e.At).UTC()
			tag, err := tx.Exec(ctx, `
				INSERT INTO events (id, type, at, player, subject, killer_kind, count, icon)
				VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
				ON CONFLICT (id) DO NOTHING`,
				e.ID, e.Type, at, e.Player, e.Subject, e.KillerKind, e.Count, e.Icon)
			if err != nil {
				return err
			}
			if tag.RowsAffected() == 0 {
				continue // seen before
			}
			if err := tally(ctx, tx, e, at); err != nil {
				return fmt.Errorf("%s %s: %w", e.Type, e.ID, err)
			}
			applied++
		}
		return nil
	})
	return applied, err
}

// tally updates the running totals for one new event.
func tally(ctx context.Context, tx pgx.Tx, e ingest.Event, at time.Time) error {
	exec := func(sql string, args ...any) error {
		_, err := tx.Exec(ctx, sql, args...)
		return err
	}
	counter := func(name string, by int) error {
		return exec(`INSERT INTO counters (name, value) VALUES ($1, $2)
			ON CONFLICT (name) DO UPDATE SET value = counters.value + EXCLUDED.value`, name, by)
	}
	if e.Player != "" {
		if err := exec(`INSERT INTO players (name, first_seen, last_seen) VALUES ($1, $2, $2)
			ON CONFLICT (name) DO UPDATE SET last_seen = GREATEST(players.last_seen, EXCLUDED.last_seen),
			                                 first_seen = LEAST(players.first_seen, EXCLUDED.first_seen)`,
			e.Player, at); err != nil {
			return err
		}
	}
	switch e.Type {
	case ingest.PlayerJoined:
		if err := counter("players_joined", 1); err != nil {
			return err
		}
		return exec(`UPDATE players SET sessions = sessions + 1 WHERE name = $1`, e.Player)
	case ingest.PlayerLeft:
		return exec(`UPDATE players SET play_seconds = play_seconds + $2 WHERE name = $1`, e.Player, e.Count)
	case ingest.WorldStarted:
		return counter("worlds", 1)
	case ingest.ChatSent:
		return counter("chat", 1)
	case ingest.ItemCollected:
		if err := exec(`INSERT INTO item_totals (item, icon, collected) VALUES ($1, $2, $3)
			ON CONFLICT (item) DO UPDATE SET collected = item_totals.collected + EXCLUDED.collected, icon = EXCLUDED.icon`,
			e.Subject, e.Icon, e.Count); err != nil {
			return err
		}
		if e.Player == "" {
			return nil
		}
		if err := exec(`INSERT INTO player_items (player, item, icon, collected) VALUES ($1, $2, $3, $4)
			ON CONFLICT (player, item) DO UPDATE SET collected = player_items.collected + EXCLUDED.collected`,
			e.Player, e.Subject, e.Icon, e.Count); err != nil {
			return err
		}
		return exec(`UPDATE players SET items_collected = items_collected + $2 WHERE name = $1`, e.Player, e.Count)
	case ingest.ItemCrafted:
		return exec(`INSERT INTO item_totals (item, icon, crafted) VALUES ($1, $2, $3)
			ON CONFLICT (item) DO UPDATE SET crafted = item_totals.crafted + EXCLUDED.crafted, icon = EXCLUDED.icon`,
			e.Subject, e.Icon, e.Count)
	case ingest.TileBroken:
		return exec(`INSERT INTO tile_totals (tile, broken) VALUES ($1, 1)
			ON CONFLICT (tile) DO UPDATE SET broken = tile_totals.broken + 1`, e.Subject)
	case ingest.MobKilled:
		if err := exec(`INSERT INTO mob_totals (mob, killed) VALUES ($1, 1)
			ON CONFLICT (mob) DO UPDATE SET killed = mob_totals.killed + 1`, e.Subject); err != nil {
			return err
		}
		if e.Player == "" {
			return nil
		}
		if err := exec(`UPDATE players SET kills = kills + 1 WHERE name = $1`, e.Player); err != nil {
			return err
		}
		return exec(`INSERT INTO player_mobs (player, mob, killed) VALUES ($1, $2, 1)
			ON CONFLICT (player, mob) DO UPDATE SET killed = player_mobs.killed + 1`, e.Player, e.Subject)
	case ingest.PlayerKilled:
		if err := exec(`UPDATE players SET deaths = deaths + 1,
			longest_life_seconds = GREATEST(longest_life_seconds, $2) WHERE name = $1`, e.Player, e.Count); err != nil {
			return err
		}
		switch e.KillerKind {
		case "player":
			if e.Subject == "" {
				return nil
			}
			if err := exec(`INSERT INTO players (name, first_seen, last_seen) VALUES ($1, $2, $2)
				ON CONFLICT (name) DO NOTHING`, e.Subject, at); err != nil {
				return err
			}
			if err := exec(`UPDATE players SET pvp_kills = pvp_kills + 1 WHERE name = $1`, e.Subject); err != nil {
				return err
			}
			return exec(`INSERT INTO kills (killer, victim, count) VALUES ($1, $2, 1)
				ON CONFLICT (killer, victim) DO UPDATE SET count = kills.count + 1`, e.Subject, e.Player)
		case "mob":
			return exec(`INSERT INTO mob_totals (mob, players_killed) VALUES ($1, 1)
				ON CONFLICT (mob) DO UPDATE SET players_killed = mob_totals.players_killed + 1`, e.Subject)
		}
		return nil
	case ingest.LevelReached:
		return exec(`UPDATE players SET deepest_level = GREATEST(deepest_level, $2) WHERE name = $1`,
			e.Player, Depth(e.Count))
	case ingest.BossDefeated:
		if err := counter("boss_defeats", 1); err != nil {
			return err
		}
		if e.Player == "" {
			return nil
		}
		return exec(`UPDATE players SET boss_kills = boss_kills + 1 WHERE name = $1`, e.Player)
	}
	return nil
}

// Depth ranks a World level for "deepest level reached": the surface is 0, the caves 1 to 3, and the sky, the
// hardest place to reach (above the deepest cave), 4. The game numbers its levels sky = 0, surface = 1, caves 2-4.
func Depth(levelIndex int) int {
	switch {
	case levelIndex == 0:
		return 4
	case levelIndex >= 2:
		return levelIndex - 1
	default:
		return 0
	}
}
