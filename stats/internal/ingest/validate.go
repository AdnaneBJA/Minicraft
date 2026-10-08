package ingest

import (
	"errors"
	"fmt"
)

// MaxEventsPerBatch bounds one POST; the game server sends about a second's worth at a time.
const MaxEventsPerBatch = 5000

var knownTypes = map[string]bool{
	TileBroken: true, ItemCollected: true, ItemCrafted: true, MobKilled: true, PlayerKilled: true,
	LevelReached: true, BossDefeated: true, PlayerJoined: true, PlayerLeft: true, WorldStarted: true, ChatSent: true,
}

var killerKinds = map[string]bool{"": true, "player": true, "mob": true, "environment": true}

// Validate checks a whole batch; one bad event rejects it all, so nothing half-valid gets stored.
func Validate(b Batch) error {
	if b.Online < 0 || b.Online > 10000 {
		return errors.New("online out of range")
	}
	if len(b.Events) > MaxEventsPerBatch {
		return fmt.Errorf("too many events (%d)", len(b.Events))
	}
	for i, e := range b.Events {
		if err := validateEvent(e); err != nil {
			return fmt.Errorf("event %d: %w", i, err)
		}
	}
	return nil
}

func validateEvent(e Event) error {
	switch {
	case len(e.ID) == 0 || len(e.ID) > 64 || !printable(e.ID):
		return errors.New("bad id")
	case !knownTypes[e.Type]:
		return fmt.Errorf("unknown type %q", e.Type)
	case e.Player != "" && !validName(e.Player):
		return errors.New("bad player name")
	case len(e.Subject) > 32 || !printable(e.Subject):
		return errors.New("bad subject")
	case !killerKinds[e.KillerKind]:
		return errors.New("bad killer kind")
	case e.Count < 0 || e.Count > 1_000_000:
		return errors.New("count out of range")
	case e.Icon < -1 || e.Icon > 255:
		return errors.New("icon out of range")
	}
	return nil
}

// validName follows the game's rule: 1 to 12 letters, digits, '-' or '_'.
func validName(name string) bool {
	if len(name) == 0 || len(name) > 12 {
		return false
	}
	for _, c := range name {
		ok := c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_'
		if !ok {
			return false
		}
	}
	return true
}

func printable(s string) bool {
	for _, c := range s {
		if c < 0x20 || c > 0x7e {
			return false
		}
	}
	return true
}
