// Package probe plays the hosted game the way monitoring needs: it joins as a hidden observer (never in the world),
// times the join, then times pings that the server answers right after its next tick (the path a key press takes).
package probe

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/coder/websocket"

	"github.com/AdnaneBJA/Minicraft/loadtest/protocol"
)

// Result is how a run ended.
type Result string

// The results, as the probe_runs_total label.
const (
	OK            Result = "ok"
	ConnectFailed Result = "connect_failed"
	Refused       Result = "refused"     // the server said no (Error)
	JoinFailed    Result = "join_failed" // the connection broke before the world arrived
	Timeout       Result = "timeout"     // the deadline passed (no world, or pongs missing)
)

// Results lists them all.
var Results = []Result{OK, ConnectFailed, Refused, JoinFailed, Timeout}

// Config is one run's settings.
type Config struct {
	URL       string
	Token     string // the server's probe token (PROBE_TOKEN)
	Pings     int
	PingEvery time.Duration
	Timeout   time.Duration // the whole run's deadline
}

// Run is what one run measured.
type Run struct {
	Result    Result
	Err       error
	Join      time.Duration   // from dialing to having the whole world
	Latencies []time.Duration // ping -> its pong, one per answered ping
	TickGaps  []time.Duration // between consecutive ticks
}

const maxMessage = 1 << 20 // anything but the history (streamed) is far smaller

// Once does one run. It never panics on a bad server; every failure is a Result.
func Once(ctx context.Context, cfg Config) Run {
	ctx, cancel := context.WithTimeout(ctx, cfg.Timeout)
	defer cancel()
	started := time.Now()
	fail := func(r Result, err error) Run {
		if ctx.Err() != nil {
			r = Timeout
		}
		return Run{Result: r, Err: err}
	}

	conn, _, err := websocket.Dial(ctx, cfg.URL, nil)
	if err != nil {
		return fail(ConnectFailed, err)
	}
	defer conn.CloseNow()
	conn.SetReadLimit(-1) // ReadMessage enforces the limits itself
	if err := conn.Write(ctx, websocket.MessageBinary, protocol.EncodeObserve(cfg.Token)); err != nil {
		return fail(JoinFailed, err)
	}
	for joined := false; !joined; {
		msg, err := read(ctx, conn)
		if err != nil {
			return fail(JoinFailed, err)
		}
		switch msg.Type {
		case protocol.Error:
			return Run{Result: Refused, Err: errors.New(msg.Text)}
		case protocol.Joined:
			joined = true
		}
	}
	run := Run{Result: OK, Join: time.Since(started)}

	// Pings go out on their own; the reads record ticks and pongs until every ping is answered.
	var mu sync.Mutex
	sent := map[int32]time.Time{}
	go func() {
		for i := 1; i <= cfg.Pings; i++ {
			mu.Lock()
			sent[int32(i)] = time.Now()
			mu.Unlock()
			if conn.Write(ctx, websocket.MessageBinary, protocol.EncodeProbePing(int32(i))) != nil {
				return
			}
			select {
			case <-ctx.Done():
				return
			case <-time.After(cfg.PingEvery):
			}
		}
	}()
	var lastTick time.Time
	for len(run.Latencies) < cfg.Pings {
		msg, err := read(ctx, conn)
		if err != nil { // keep what was measured; the run still failed
			failed := fail(JoinFailed, fmt.Errorf("after %d of %d pongs: %w", len(run.Latencies), cfg.Pings, err))
			run.Result, run.Err = failed.Result, failed.Err
			return run
		}
		now := time.Now()
		switch msg.Type {
		case protocol.Tick:
			if !lastTick.IsZero() {
				run.TickGaps = append(run.TickGaps, now.Sub(lastTick))
			}
			lastTick = now
		case protocol.Joined:
			lastTick = time.Time{} // a world reset: the tick clock restarts
		case protocol.ProbePong:
			// The server keeps one ping per observer: a newer one replaces one it hasn't answered yet. The tick
			// that answered this one would have answered those too, so they all get this answer's time.
			mu.Lock()
			for id, at := range sent {
				if id <= msg.PingID {
					run.Latencies = append(run.Latencies, now.Sub(at))
					delete(sent, id)
				}
			}
			mu.Unlock()
		}
	}
	_ = conn.Close(websocket.StatusNormalClosure, "")
	return run
}

func read(ctx context.Context, conn *websocket.Conn) (protocol.Message, error) {
	_, r, err := conn.Reader(ctx)
	if err != nil {
		return protocol.Message{}, err
	}
	msg, _, err := protocol.ReadMessage(r, maxMessage)
	return msg, err
}
