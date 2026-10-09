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
// every second, empty or not, so it doubles as a heartbeat. Health is how the server is doing (absent from servers
// older than it).
type Batch struct {
	Online int     `json:"online"`
	Events []Event `json:"events"`
	Health *Health `json:"health"`
}

// Health is the game server's state when it cut the batch.
type Health struct {
	At           int64   `json:"at"` // Unix milliseconds, when the batch was cut
	Connections  int     `json:"connections"`
	Observers    int     `json:"observers"`
	Ticks        int64   `json:"ticks"`
	HistoryTicks int64   `json:"historyTicks"`
	HistoryBytes int64   `json:"historyBytes"`
	Backlog      int     `json:"backlog"`
	RSSBytes     int64   `json:"rssBytes"`
	CPUSeconds   float64 `json:"cpuSeconds"`
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
