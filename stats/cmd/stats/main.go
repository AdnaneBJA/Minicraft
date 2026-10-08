// stats: the Minicraft stats service. The game server posts what players do to /events; the dashboard at /stats
// shows it.
//
// Environment:
//
//	DATABASE_URL  postgres://... (required)
//	STATS_TOKEN   the secret the game server sends (required)
//	LISTEN        address to listen on (default :8080)
//	SPRITES_DIR   the game's sprite sheets, for icons (default /sprites)
package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/AdnaneBJA/Minicraft/stats/internal/server"
	"github.com/AdnaneBJA/Minicraft/stats/internal/store"
	"github.com/AdnaneBJA/Minicraft/stats/internal/web"
)

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}

func run() error {
	databaseURL := os.Getenv("DATABASE_URL")
	if databaseURL == "" {
		return errors.New("DATABASE_URL is not set")
	}
	token := os.Getenv("STATS_TOKEN")
	if token == "" {
		return server.ErrNoToken
	}
	listen := envOr("LISTEN", ":8080")

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// The database may still be starting (docker compose): keep trying for a minute.
	var st *store.Store
	var err error
	for attempt := 0; attempt < 30; attempt++ {
		if st, err = store.Open(ctx, databaseURL); err == nil {
			break
		}
		log.Printf("waiting for the database: %v", err)
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(2 * time.Second):
		}
	}
	if err != nil {
		return err
	}
	defer st.Close()

	// The pages show the players online, which the ingestion side knows: hence the late-bound closure.
	var srv *server.Server
	public := web.Handler(st, func() int { return srv.Online() }, envOr("SPRITES_DIR", "/sprites"))
	srv = server.New(st, token, public)
	httpServer := &http.Server{Addr: listen, Handler: srv, ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = httpServer.Shutdown(shutdown)
	}()
	log.Printf("stats service listening on %s", listen)
	if err := httpServer.ListenAndServe(); !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return nil
}

func envOr(name, fallback string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return fallback
}
