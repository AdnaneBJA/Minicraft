package metrics

import "sync/atomic"

// Counters are the run's running totals, safe to update from any goroutine.
type Counters struct {
	Connected  atomic.Int64 // bots connected right now
	Joined     atomic.Int64 // joins completed (Welcome + Joined received)
	JoinFailed atomic.Int64 // refused by the game ("Name already in use", ...)
	Refused    atomic.Int64 // connections that failed or were closed before joining
	Dropped    atomic.Int64 // connections lost after joining
	Messages   atomic.Int64 // messages received
	Bytes      atomic.Int64 // bytes received
	Sent       atomic.Int64 // messages sent
	ChatLines  atomic.Int64 // chat lines received
	Superseded atomic.Int64 // key changes overtaken by the next one before the server echoed them
}

// CountersSnapshot is the counters at one moment, for reports.
type CountersSnapshot struct {
	Connected, Joined, JoinFailed, Refused, Dropped, Messages, Bytes, Sent, ChatLines, Superseded int64
}

// Snapshot reads every counter.
func (c *Counters) Snapshot() CountersSnapshot {
	return CountersSnapshot{
		Connected: c.Connected.Load(), Joined: c.Joined.Load(), JoinFailed: c.JoinFailed.Load(),
		Refused: c.Refused.Load(), Dropped: c.Dropped.Load(), Messages: c.Messages.Load(), Bytes: c.Bytes.Load(),
		Sent: c.Sent.Load(), ChatLines: c.ChatLines.Load(), Superseded: c.Superseded.Load(),
	}
}
