// Package bot is one load-test player: it joins over the real protocol, walks, attacks and chats like a person, and
// measures what a person would feel (input latency, tick timing, join time).
package bot

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/rand/v2"
	"sync"
	"sync/atomic"
	"time"

	"github.com/coder/websocket"

	"github.com/AdnaneBJA/Minicraft/loadtest/metrics"
	"github.com/AdnaneBJA/Minicraft/loadtest/protocol"
)

const (
	tickLength   = time.Second / 60
	dialTimeout  = 5 * time.Second
	writeTimeout = time.Second
	maxMessage   = 1 << 20 // ordinary messages; the world's history (any size) is streamed instead
	joinTimeout  = 10 * time.Second
)

// Shared is where every bot records: the histograms and counters of the whole run.
type Shared struct {
	Latency  *metrics.Histogram // key change sent -> first tick carrying it
	Jitter   *metrics.Histogram // |gap between ticks - 16.7 ms|
	Join     *metrics.Histogram // connect -> world received
	Counters *metrics.Counters
	// StepLatency, when set, also gets every input latency (a ramp swaps in a fresh one per step).
	StepLatency atomic.Pointer[metrics.Histogram]
	// LastTick is when any bot last received a tick (Unix nanoseconds): a wedged server stops it moving.
	LastTick atomic.Int64
}

// Config is one bot.
type Config struct {
	URL         string
	Name        string
	Seed        int64
	ChangeEvery time.Duration // average time between key changes (default 1.25 s: 0.5-2 s)
	ChatEvery   time.Duration // average time between chat lines (default 40 s; negative: never)
	// JoinOnly bots leave as soon as they have the world (soak-mode probes); OnJoined, if set, hears how long the
	// join took and how many ticks of history came with it.
	JoinOnly    bool
	JoinTimeout time.Duration // a join that takes longer failed (default 10 s): the server may be wedged
	OnJoined    func(took time.Duration, historyLen int)
}

// ErrJoinRefused: the game refused the bot (its name, say).
var ErrJoinRefused = errors.New("join refused")

// pendingKeys is a key change waiting for the server to show it in a tick.
type pendingKeys struct {
	mu       sync.Mutex
	keys     protocol.Keys
	sentAt   time.Time
	active   bool
	lastEcho protocol.Keys // the keys the latest tick showed for this bot
}

// Run plays until ctx ends (nil) or the connection fails (an error). Everything it sees goes into s.
func Run(ctx context.Context, cfg Config, s *Shared) error {
	if cfg.ChangeEvery <= 0 {
		cfg.ChangeEvery = 1250 * time.Millisecond
	}
	if cfg.ChatEvery == 0 {
		cfg.ChatEvery = 40 * time.Second
	}
	started := time.Now()
	dialCtx, cancelDial := context.WithTimeout(ctx, dialTimeout)
	conn, _, err := websocket.Dial(dialCtx, cfg.URL, nil)
	cancelDial()
	if err != nil {
		if ctx.Err() != nil {
			return nil
		}
		s.Counters.Refused.Add(1)
		return fmt.Errorf("%s: connect: %w", cfg.Name, err)
	}
	defer conn.CloseNow()
	conn.SetReadLimit(-1) // readMessage enforces the limits itself (the history can be hundreds of MB)

	if cfg.JoinTimeout <= 0 {
		cfg.JoinTimeout = joinTimeout
	}
	joinCtx, cancelJoin := context.WithTimeout(ctx, cfg.JoinTimeout)
	id, historyLen, err := join(joinCtx, conn, cfg.Name, s)
	cancelJoin()
	if err != nil {
		if ctx.Err() != nil {
			return nil
		}
		return err
	}
	took := time.Since(started)
	s.Join.Record(took)
	s.Counters.Joined.Add(1)
	if cfg.OnJoined != nil {
		cfg.OnJoined(took, historyLen)
	}
	if cfg.JoinOnly {
		_ = conn.Close(websocket.StatusNormalClosure, "probe done")
		return nil
	}
	s.Counters.Connected.Add(1)
	defer s.Counters.Connected.Add(-1)

	playCtx, stopPlaying := context.WithCancel(ctx)
	defer stopPlaying()
	pending := &pendingKeys{}
	go act(playCtx, conn, cfg, pending, s)

	err = read(ctx, conn, id, pending, s)
	if ctx.Err() != nil {
		_ = conn.Close(websocket.StatusNormalClosure, "load test over") // the server sees a normal leave
		return nil
	}
	s.Counters.Dropped.Add(1)
	return fmt.Errorf("%s: connection lost: %w", cfg.Name, err)
}

// join says Hello and waits for Welcome and the world (Joined).
func join(ctx context.Context, conn *websocket.Conn, name string, s *Shared) (int32, int, error) {
	if err := write(ctx, conn, protocol.EncodeHello(name), s); err != nil {
		s.Counters.Refused.Add(1)
		return 0, 0, fmt.Errorf("%s: hello: %w", name, err)
	}
	var id int32
	for {
		msg, err := readMessage(ctx, conn, s)
		if errors.Is(err, protocol.ErrMalformed) {
			continue
		}
		if err != nil {
			// Connected, but the world never came: closed on us, too big to take, or the server is wedged.
			s.Counters.JoinFailed.Add(1)
			return 0, 0, fmt.Errorf("%s: joining: %w", name, err)
		}
		switch msg.Type {
		case protocol.Welcome:
			id = msg.PlayerID
		case protocol.Joined:
			return id, msg.HistoryLen, nil
		case protocol.Error:
			s.Counters.JoinFailed.Add(1)
			return 0, 0, fmt.Errorf("%s: %w: %s", name, ErrJoinRefused, msg.Text)
		}
	}
}

// read handles everything the server sends, until the connection ends.
func read(ctx context.Context, conn *websocket.Conn, id int32, pending *pendingKeys, s *Shared) error {
	var lastTick time.Time
	for {
		msg, err := readMessage(ctx, conn, s)
		if errors.Is(err, protocol.ErrMalformed) {
			continue
		}
		if err != nil {
			return err
		}
		now := time.Now()
		switch msg.Type {
		case protocol.Tick:
			if !lastTick.IsZero() {
				gap := now.Sub(lastTick) - tickLength
				if gap < 0 {
					gap = -gap
				}
				s.Jitter.Record(gap)
			}
			lastTick = now
			s.LastTick.Store(now.UnixNano())
			for _, turn := range msg.Tick.Turns {
				if turn.PlayerID == id {
					pending.resolve(turn.Keys, now, s)
				}
			}
		case protocol.ChatLine:
			s.Counters.ChatLines.Add(1)
		}
	}
}

// readMessage reads one message. The world's history (Joined) is streamed: its header is read and the rest only
// counted, so a history of any size never sits in memory. Every other message must fit in maxMessage.
func readMessage(ctx context.Context, conn *websocket.Conn, s *Shared) (protocol.Message, error) {
	_, r, err := conn.Reader(ctx)
	if err != nil {
		return protocol.Message{}, err
	}
	var header [9]byte // type, then Joined's seed and tick count
	n, err := io.ReadFull(r, header[:1])
	if err != nil {
		return protocol.Message{}, err
	}
	if protocol.MsgType(header[0]) == protocol.Joined {
		if _, err := io.ReadFull(r, header[1:]); err != nil {
			return protocol.Message{}, err
		}
		rest, err := io.Copy(io.Discard, r)
		if err != nil {
			return protocol.Message{}, err
		}
		s.Counters.Messages.Add(1)
		s.Counters.Bytes.Add(int64(len(header)) + rest)
		count := int32(binary.LittleEndian.Uint32(header[5:]))
		if count < 0 {
			return protocol.Message{}, fmt.Errorf("%w: Joined history count %d", protocol.ErrMalformed, count)
		}
		return protocol.Message{Type: protocol.Joined, Seed: binary.LittleEndian.Uint32(header[1:]),
			HistoryLen: int(count)}, nil
	}
	body, err := io.ReadAll(io.LimitReader(r, maxMessage))
	if err != nil {
		return protocol.Message{}, err
	}
	if extra, _ := io.Copy(io.Discard, r); extra > 0 {
		return protocol.Message{}, fmt.Errorf("message over %d bytes", maxMessage)
	}
	data := append(header[:n:n], body...)
	s.Counters.Messages.Add(1)
	s.Counters.Bytes.Add(int64(len(data)))
	return protocol.Decode(data)
}

// The attack-pressed bit is a one-tick pulse the server adds on its own; compare the keys without it.
const held = ^protocol.KeyPressed

func (p *pendingKeys) set(k protocol.Keys, at time.Time, s *Shared) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.active {
		s.Counters.Superseded.Add(1) // the previous change never showed up before this one
	}
	p.keys, p.sentAt, p.active = k, at, true
}

func (p *pendingKeys) resolve(k protocol.Keys, now time.Time, s *Shared) {
	p.mu.Lock()
	defer p.mu.Unlock()
	// Only a change in what the server shows counts: if the server is behind and still shows old keys that happen
	// to equal the pending ones, that stale echo confirms nothing.
	changed := k&held != p.lastEcho
	p.lastEcho = k & held
	if p.active && changed && k&held == p.keys&held {
		latency := now.Sub(p.sentAt)
		s.Latency.Record(latency)
		if step := s.StepLatency.Load(); step != nil {
			step.Record(latency)
		}
		p.active = false
	}
}

// act plays: a random walk, attacking now and then, an occasional chat line. Only key changes are sent, like the
// game does. Writes time out, so a slow server can't stall the bot (the reader is a separate goroutine).
func act(ctx context.Context, conn *websocket.Conn, cfg Config, pending *pendingKeys, s *Shared) {
	rng := rand.New(rand.NewPCG(uint64(cfg.Seed), 0x9e3779b97f4a7c15))
	jittered := func(mean time.Duration) time.Duration {
		return time.Duration(float64(mean) * (0.4 + 1.2*rng.Float64()))
	}
	directions := []protocol.Keys{0, protocol.KeyLeft, protocol.KeyRight, protocol.KeyUp, protocol.KeyDown,
		protocol.KeyLeft | protocol.KeyUp, protocol.KeyLeft | protocol.KeyDown, protocol.KeyRight | protocol.KeyUp,
		protocol.KeyRight | protocol.KeyDown}
	var keys protocol.Keys
	nextChange := time.After(jittered(cfg.ChangeEvery))
	var nextChat <-chan time.Time
	if cfg.ChatEvery > 0 {
		nextChat = time.After(jittered(cfg.ChatEvery))
	}
	for {
		select {
		case <-ctx.Done():
			return
		case <-nextChange:
			next := keys
			for next&held == keys&held { // always an actual change
				next = directions[rng.IntN(len(directions))]
				if rng.Float64() < 0.2 {
					next |= protocol.KeyAttack
					if keys&protocol.KeyAttack == 0 {
						next |= protocol.KeyPressed
					}
				}
			}
			keys = next
			pending.set(keys, time.Now(), s)
			if err := write(ctx, conn, protocol.EncodeInput(keys), s); err != nil && ctx.Err() != nil {
				return
			}
			nextChange = time.After(jittered(cfg.ChangeEvery))
		case <-nextChat:
			_ = write(ctx, conn, protocol.EncodeChat(fmt.Sprintf("%s says hi", cfg.Name)), s)
			nextChat = time.After(jittered(cfg.ChatEvery))
		}
	}
}

func write(ctx context.Context, conn *websocket.Conn, data []byte, s *Shared) error {
	writeCtx, cancel := context.WithTimeout(ctx, writeTimeout)
	defer cancel()
	if err := conn.Write(writeCtx, websocket.MessageBinary, data); err != nil {
		return err
	}
	s.Counters.Sent.Add(1)
	return nil
}
