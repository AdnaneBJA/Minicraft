// Package fakeserver plays minicraft-server's part for tests: Welcome and Joined on Hello, then 60 Hz ticks carrying
// every player's latest keys, applied after an adjustable delay (so a measured input latency can be checked).
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

	mu        sync.Mutex
	players   map[int32]*player
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

type keyChange struct {
	keys protocol.Keys
	at   time.Time
}

// Start runs a fake server until the test ends.
func Start(t testing.TB) *Server {
	ctx, cancel := context.WithCancel(context.Background())
	s := &Server{players: map[int32]*player{}, RejectNames: map[string]bool{}, cancel: cancel}
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
		if err != nil || msg.Type != protocol.Hello {
			continue
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
	_ = conn.Write(ctx, websocket.MessageBinary, protocol.EncodeJoined(7, nil))

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

func (s *Server) tickLoop(ctx context.Context) {
	ticker := time.NewTicker(tickLength)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-ticker.C:
			s.mu.Lock()
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
			s.mu.Unlock()
			data := protocol.EncodeTick(msg)
			for _, p := range targets {
				writeCtx, cancel := context.WithTimeout(p.ctx, time.Second)
				_ = p.conn.Write(writeCtx, websocket.MessageBinary, data)
				cancel()
			}
		}
	}
}
