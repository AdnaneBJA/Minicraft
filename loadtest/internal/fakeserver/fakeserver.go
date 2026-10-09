// Package fakeserver plays minicraft-server's part for tests: Welcome and Joined on Hello, then 60 Hz ticks carrying
// every player's latest keys, applied after an adjustable delay (so a measured input latency can be checked).
// Observers (monitoring probes) get Joined and the ticks, and a pong for each ping after an adjustable delay.
package fakeserver

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/coder/websocket"

	"github.com/AdnaneBJA/Minicraft/loadtest/protocol"
)

const tickLength = time.Second / 60

// Server is a fake game server on a local port.
type Server struct {
	URL         string                 // ws://127.0.0.1:port
	Delay       time.Duration          // how long a key change takes to show up in the ticks
	RejectNames map[string]bool        // Hello with these names gets "Name already in use"
	MaxPlayers  int                    // connections beyond this are refused (0: no limit)
	OnJoin      func(name string) bool // optional: return false to refuse the join
	History     []protocol.TickMsg     // sent with Joined (an aging world)
	Stalled     bool                   // set before traffic: never answers Hello and sends no ticks (a wedged server)

	RefuseObservers bool          // Observe gets "Too many observers"
	ObserveToken    string        // Observe must carry it, or gets "Not allowed to observe"
	PongDelay       time.Duration // a ping is answered with the first tick at least this long after it

	mu        sync.Mutex
	players   map[int32]*player
	observers map[*observer]bool
	nextID    int32
	tick      int32
	refuseAll bool
	http      *httptest.Server
	cancel    context.CancelFunc
}

type player struct {
	conn        *websocket.Conn
	keys        protocol.Keys
	pending     []keyChange
	readPaused  bool
	ctx         context.Context
	closeSocket context.CancelFunc
}

type observer struct {
	conn    *websocket.Conn
	ctx     context.Context
	pending []pendingPing
}

type pendingPing struct {
	id int32
	at time.Time
}

type keyChange struct {
	keys protocol.Keys
	at   time.Time
}

// Start runs a fake server until the test ends.
func Start(t testing.TB) *Server {
	ctx, cancel := context.WithCancel(context.Background())
	s := &Server{players: map[int32]*player{}, observers: map[*observer]bool{}, RejectNames: map[string]bool{},
		cancel: cancel}
	s.http = httptest.NewServer(http.HandlerFunc(s.serve))
	s.URL = "ws" + strings.TrimPrefix(s.http.URL, "http")
	go s.tickLoop(ctx)
	t.Cleanup(func() {
		cancel()
		s.CloseAll()
		s.http.Close()
	})
	return s
}

// CloseAll drops every connection, as a crashing server would.
func (s *Server) CloseAll() {
	s.mu.Lock()
	defer s.mu.Unlock()
	for id, p := range s.players {
		p.closeSocket()
		_ = p.conn.CloseNow()
		delete(s.players, id)
	}
}

// RefuseConnections makes new connections fail, as a full server does.
func (s *Server) RefuseConnections() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.refuseAll = true
}

// Stall makes the server stop answering and ticking, as a wedged server would.
func (s *Server) Stall() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Stalled = true
}

// PauseReading stops reading what clients send (their messages pile up), while ticks keep going out.
func (s *Server) PauseReading(d time.Duration) {
	s.mu.Lock()
	for _, p := range s.players {
		p.readPaused = true
	}
	s.mu.Unlock()
	time.AfterFunc(d, func() {
		s.mu.Lock()
		defer s.mu.Unlock()
		for _, p := range s.players {
			p.readPaused = false
		}
	})
}

// Players is how many are in the world.
func (s *Server) Players() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.players)
}

func (s *Server) serve(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	refuse := s.refuseAll || (s.MaxPlayers > 0 && len(s.players) >= s.MaxPlayers)
	s.mu.Unlock()
	if refuse {
		http.Error(w, "full", http.StatusServiceUnavailable)
		return
	}
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	conn.SetReadLimit(1 << 20)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	defer conn.CloseNow()

	// Hello first; a refused name gets an Error and may try again.
	var id int32
	var me *player
	for me == nil {
		_, data, err := conn.Read(ctx)
		if err != nil {
			return
		}
		msg, err := protocol.Decode(data)
		if err == nil && msg.Type == protocol.Observe {
			s.serveObserver(ctx, conn, msg.Text)
			return
		}
		if err != nil || msg.Type != protocol.Hello {
			continue
		}
		s.mu.Lock()
		stalled := s.Stalled
		s.mu.Unlock()
		if stalled {
			continue // a wedged server never answers
		}
		if s.RejectNames[msg.Text] || (s.OnJoin != nil && !s.OnJoin(msg.Text)) {
			_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeError("Name already in use"))
			continue
		}
		s.mu.Lock()
		s.nextID++
		id = s.nextID
		me = &player{conn: conn, ctx: ctx, closeSocket: cancel}
		s.players[id] = me
		s.mu.Unlock()
	}
	defer func() {
		s.mu.Lock()
		delete(s.players, id)
		s.mu.Unlock()
	}()
	_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeWelcome(id))
	_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeJoined(7, s.History))

	for {
		s.mu.Lock()
		paused := me.readPaused
		s.mu.Unlock()
		if paused {
			select {
			case <-ctx.Done():
				return
			case <-time.After(10 * time.Millisecond):
			}
			continue
		}
		_, data, err := conn.Read(ctx)
		if err != nil {
			return
		}
		msg, err := protocol.Decode(data)
		if err != nil {
			continue
		}
		if msg.Type == protocol.Input {
			s.mu.Lock()
			me.pending = append(me.pending, keyChange{keys: msg.Keys, at: time.Now()})
			s.mu.Unlock()
		}
	}
}

// serveObserver: the world (Joined), then every tick, and a pong after the first tick past PongDelay.
func (s *Server) serveObserver(ctx context.Context, conn *websocket.Conn, token string) {
	s.mu.Lock()
	refuse, stalled, wrongToken := s.RefuseObservers, s.Stalled, token != s.ObserveToken
	s.mu.Unlock()
	if stalled { // a wedged server never answers; read until the client gives up
		for {
			if _, _, err := conn.Read(ctx); err != nil {
				return
			}
		}
	}
	if wrongToken {
		_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeError("Not allowed to observe"))
		return
	}
	if refuse {
		_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeError("Too many observers"))
		return
	}
	_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeJoined(7, s.History))
	me := &observer{conn: conn, ctx: ctx}
	s.mu.Lock()
	s.observers[me] = true
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		delete(s.observers, me)
		s.mu.Unlock()
	}()
	for {
		_, data, err := conn.Read(ctx)
		if err != nil {
			return
		}
		if msg, err := protocol.Decode(data); err == nil && msg.Type == protocol.ProbePing {
			s.mu.Lock()
			me.pending = []pendingPing{{id: msg.PingID, at: time.Now()}} // like the real server: the newest replaces one waiting
			s.mu.Unlock()
		}
	}
}

// ResetWorld sends every observer a new Joined, as a world reset does.
func (s *Server) ResetWorld() {
	s.mu.Lock()
	var targets []*observer
	for o := range s.observers {
		targets = append(targets, o)
	}
	s.mu.Unlock()
	for _, o := range targets {
		_ = o.conn.Write(o.ctx, websocket.MessageBinary, protocol.EncodeJoined(8, nil))
	}
}

func (s *Server) tickLoop(ctx context.Context) {
	ticker := time.NewTicker(tickLength)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-ticker.C:
			s.mu.Lock()
			if s.Stalled {
				s.mu.Unlock()
				continue
			}
			s.tick++
			msg := protocol.TickMsg{Tick: s.tick}
			var targets []*player
			for id, p := range s.players {
				// Key changes older than Delay take effect now.
				for len(p.pending) > 0 && now.Sub(p.pending[0].at) >= s.Delay {
					p.keys = p.pending[0].keys
					p.pending = p.pending[1:]
				}
				msg.Turns = append(msg.Turns, protocol.Turn{PlayerID: id, Keys: p.keys})
				targets = append(targets, p)
			}
			type pong struct {
				o  *observer
				id int32
			}
			var watchers []*observer
			var pongs []pong
			for o := range s.observers {
				watchers = append(watchers, o)
				for len(o.pending) > 0 && now.Sub(o.pending[0].at) >= s.PongDelay {
					pongs = append(pongs, pong{o, o.pending[0].id})
					o.pending = o.pending[1:]
				}
			}
			s.mu.Unlock()
			data := protocol.EncodeTick(msg)
			send := func(conn *websocket.Conn, connCtx context.Context, b []byte) {
				writeCtx, cancel := context.WithTimeout(connCtx, time.Second)
				_ = conn.Write(writeCtx, websocket.MessageBinary, b)
				cancel()
			}
			for _, p := range targets {
				send(p.conn, p.ctx, data)
			}
			for _, o := range watchers {
				send(o.conn, o.ctx, data)
			}
			for _, p := range pongs {
				send(p.o.conn, p.o.ctx, protocol.EncodeProbePong(p.id))
			}
		}
	}
}
