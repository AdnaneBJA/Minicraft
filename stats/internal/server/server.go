// Package server is the stats service's HTTP side: POST /events from the game server, GET /metrics for Prometheus,
// and the public pages.
package server

import (
	"crypto/subtle"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/AdnaneBJA/Minicraft/stats/internal/ingest"
	"github.com/AdnaneBJA/Minicraft/stats/internal/metrics"
	"github.com/AdnaneBJA/Minicraft/stats/internal/store"
)

const (
	maxBody       = 4 << 20          // 4 MB: far more than a second of events
	onlineTimeout = 30 * time.Second // no batch for this long: the game server is gone
)

// Server routes /events to ingestion and everything else to `public` (the API and the dashboard).
type Server struct {
	store   *store.Store
	token   string
	public  http.Handler
	metrics *metrics.Metrics
	now     func() time.Time

	mu         sync.Mutex
	online     int
	lastOnline time.Time
}

// New returns the service's handler. `token` is what the game server must send as "Authorization: Bearer <token>".
func New(st *store.Store, token string, public http.Handler, m *metrics.Metrics) *Server {
	return &Server{store: st, token: token, public: public, metrics: m, now: time.Now}
}

// Online is the number of players in the world, as of the game server's last batch; 0 if it went quiet.
func (s *Server) Online() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.now().Sub(s.lastOnline) > onlineTimeout {
		return 0
	}
	return s.online
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	switch r.URL.Path {
	case "/events":
		s.events(w, r)
	case "/metrics":
		// Not routed by Caddy: only Prometheus, inside the Docker network, reaches it.
		s.metrics.Handler().ServeHTTP(w, r)
	default:
		s.public.ServeHTTP(w, r)
	}
}

func (s *Server) events(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	if !s.authorized(r) {
		s.metrics.Batch("unauthorized")
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	var batch ingest.Batch
	body := http.MaxBytesReader(w, r.Body, maxBody)
	if err := json.NewDecoder(body).Decode(&batch); err != nil {
		s.metrics.Batch("invalid")
		http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	if _, err := io.Copy(io.Discard, body); err != nil {
		s.metrics.Batch("invalid")
		http.Error(w, "bad body", http.StatusBadRequest)
		return
	}
	if err := ingest.Validate(batch); err != nil {
		log.Printf("rejected a batch: %v", err)
		s.metrics.Batch("invalid")
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	s.online, s.lastOnline = batch.Online, s.now()
	s.mu.Unlock()
	s.metrics.Report(batch.Online, batch.Health, s.now())

	started := time.Now()
	applied, err := s.store.Apply(r.Context(), batch.Events)
	s.metrics.DBWrite(time.Since(started))
	if err != nil {
		s.metrics.Batch("error")
		// The database is down or restarting: the game server keeps the batch and sends it again.
		log.Printf("could not store a batch: %v", err)
		http.Error(w, "storage unavailable", http.StatusServiceUnavailable)
		return
	}
	if len(batch.Events) > 0 && applied == 0 {
		s.metrics.Batch("duplicate")
	} else {
		s.metrics.Batch("accepted")
		s.metrics.Events(batch.Events)
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]int{"applied": applied})
}

func (s *Server) authorized(r *http.Request) bool {
	token, ok := strings.CutPrefix(r.Header.Get("Authorization"), "Bearer ")
	if !ok || s.token == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(token), []byte(s.token)) == 1
}

// ErrNoToken is returned by main when STATS_TOKEN is missing: without it anyone could post events.
var ErrNoToken = errors.New("STATS_TOKEN is not set")
