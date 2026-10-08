// Package ingest is what the game server sends: batches of named events, and the rules they must follow.
package ingest

// Event is one thing that happened in the game, as the game server reports it. ID makes it count once however many
// times it's sent.
type Event struct {
	ID         string `json:"id"`
	Type       string `json:"type"`
	At         int64  `json:"at"`         // Unix milliseconds
	Player     string `json:"player"`     // who did it (may be empty)
	Subject    string `json:"subject"`    // the item, tile or mob, or the killer's name in PvP
	KillerKind string `json:"killerKind"` // PlayerKilled: "player", "mob" or "environment"
	Count      int    `json:"count"`      // how many / mob level / seconds alive or played / World level index
	Icon       int    `json:"icon"`       // items: their place in items.png (-1: none)
}

// Batch is one POST from the game server. Online is how many players are in the world right now: batches come
// every second, empty or not, so it doubles as a heartbeat.
type Batch struct {
	Online int     `json:"online"`
	Events []Event `json:"events"`
}

// The event types the game server sends.
const (
	TileBroken    = "TileBroken"
	ItemCollected = "ItemCollected"
	ItemCrafted   = "ItemCrafted"
	MobKilled     = "MobKilled"
	PlayerKilled  = "PlayerKilled"
	LevelReached  = "LevelReached"
	BossDefeated  = "BossDefeated"
	PlayerJoined  = "PlayerJoined"
	PlayerLeft    = "PlayerLeft"
	WorldStarted  = "WorldStarted"
	ChatSent      = "ChatSent"
)
