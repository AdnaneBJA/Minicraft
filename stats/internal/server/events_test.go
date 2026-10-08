package server

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/AdnaneBJA/Minicraft/stats/internal/store"
	"github.com/AdnaneBJA/Minicraft/stats/internal/testdb"
)

func TestMain(m *testing.M) {
	code := m.Run()
	testdb.Stop()
	os.Exit(code)
}

func newServer(t *testing.T) (*Server, *store.Store) {
	t.Helper()
	st, err := store.Open(context.Background(), testdb.New(t))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(st.Close)
	return New(st, "secret", http.NotFoundHandler()), st
}

func post(h http.Handler, token, body string) *httptest.ResponseRecorder {
	req := httptest.NewRequest(http.MethodPost, "/events", strings.NewReader(body))
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	return rec
}

const goodBatch = `{"online":2,"events":[{"id":"1-1-0","type":"PlayerJoined","at":1,"player":"Alice","subject":"",` +
	`"killerKind":"","count":1,"icon":-1}]}`

func TestEventsNeedToken(t *testing.T) {
	srv, _ := newServer(t)
	if rec := post(srv, "", goodBatch); rec.Code != http.StatusUnauthorized {
		t.Errorf("no token: %d", rec.Code)
	}
	if rec := post(srv, "wrong", goodBatch); rec.Code != http.StatusUnauthorized {
		t.Errorf("wrong token: %d", rec.Code)
	}
}

func TestIngestAccepts(t *testing.T) {
	srv, _ := newServer(t)
	rec := post(srv, "secret", goodBatch)
	if rec.Code != http.StatusOK {
		t.Fatalf("status %d: %s", rec.Code, rec.Body)
	}
	var reply struct{ Applied int }
	if err := json.Unmarshal(rec.Body.Bytes(), &reply); err != nil || reply.Applied != 1 {
		t.Fatalf("reply %s (%v)", rec.Body, err)
	}
	if rec := post(srv, "secret", goodBatch); !strings.Contains(rec.Body.String(), `"applied":0`) {
		t.Errorf("the same batch again: %s", rec.Body)
	}
}

func TestIngestRejectsBadBatch(t *testing.T) {
	srv, st := newServer(t)
	bad := `{"online":1,"events":[{"id":"1","type":"PlayerJoined","player":"Alice","count":1},` +
		`{"id":"2","type":"Hacked","player":"Alice","count":999}]}`
	if rec := post(srv, "secret", bad); rec.Code != http.StatusBadRequest {
		t.Fatalf("status %d", rec.Code)
	}
	if rec := post(srv, "secret", "{not json"); rec.Code != http.StatusBadRequest {
		t.Fatalf("garbage: %d", rec.Code)
	}
	summary, err := st.Summary(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if summary.PlayersJoined != 0 {
		t.Errorf("a rejected batch stored something: %+v", summary)
	}
}

func TestIngestUnavailable503(t *testing.T) {
	srv, st := newServer(t)
	st.Close()
	if rec := post(srv, "secret", goodBatch); rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("status %d", rec.Code)
	}
}

func TestOnlineExpires(t *testing.T) {
	srv, _ := newServer(t)
	now := time.Unix(1000, 0)
	srv.now = func() time.Time { return now }
	if rec := post(srv, "secret", goodBatch); rec.Code != http.StatusOK {
		t.Fatalf("status %d", rec.Code)
	}
	if got := srv.Online(); got != 2 {
		t.Errorf("online = %d, want 2", got)
	}
	now = now.Add(29 * time.Second)
	if got := srv.Online(); got != 2 {
		t.Errorf("after 29 s online = %d, want 2", got)
	}
	now = now.Add(2 * time.Second)
	if got := srv.Online(); got != 0 {
		t.Errorf("after 31 s of silence online = %d, want 0", got)
	}
}
