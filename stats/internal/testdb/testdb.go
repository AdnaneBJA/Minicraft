// Package testdb gives tests a real PostgreSQL: the one in TEST_DATABASE_URL (CI), or else an embedded one started
// for the test binary. Every call to New makes a fresh, empty database, so tests don't see each other's data.
package testdb

import (
	"context"
	"fmt"
	"math/rand/v2"
	"net"
	"os"
	"path/filepath"
	"sync"
	"testing"

	embeddedpostgres "github.com/fergusstrange/embedded-postgres"
	"github.com/jackc/pgx/v5"
)

var (
	once     sync.Once
	adminURL string
	startErr error
	embedded *embeddedpostgres.EmbeddedPostgres
)

// New returns the URL of a new empty database.
func New(t *testing.T) string {
	t.Helper()
	once.Do(start)
	if startErr != nil {
		t.Fatalf("no PostgreSQL for tests: %v", startErr)
	}
	name := fmt.Sprintf("t_%d", rand.Uint64())
	ctx := context.Background()
	conn, err := pgx.Connect(ctx, adminURL)
	if err != nil {
		t.Fatalf("connect: %v", err)
	}
	defer conn.Close(ctx)
	if _, err := conn.Exec(ctx, "CREATE DATABASE "+name); err != nil {
		t.Fatalf("create database: %v", err)
	}
	return replaceDatabase(adminURL, name)
}

// Stop shuts the embedded PostgreSQL down, if one was started. Call it from TestMain after m.Run.
func Stop() {
	if embedded != nil {
		_ = embedded.Stop()
	}
}

func start() {
	if url := os.Getenv("TEST_DATABASE_URL"); url != "" {
		adminURL = url
		return
	}
	port, err := freePort()
	if err != nil {
		startErr = err
		return
	}
	dir, err := os.MkdirTemp("", "stats-pg-")
	if err != nil {
		startErr = err
		return
	}
	cache := filepath.Join(os.TempDir(), "stats-embedded-postgres-cache")
	embedded = embeddedpostgres.NewDatabase(embeddedpostgres.DefaultConfig().
		Version(embeddedpostgres.V17). // what production runs
		Port(uint32(port)).
		RuntimePath(filepath.Join(dir, "runtime")).
		DataPath(filepath.Join(dir, "data")).
		CachePath(cache).
		Logger(nil))
	// Test binaries run in parallel and share the download cache: if another one was unpacking it at the same
	// moment, the second try finds it ready.
	err = embedded.Start()
	if err != nil {
		err = embedded.Start()
	}
	if err != nil {
		startErr = err
		embedded = nil
		return
	}
	adminURL = fmt.Sprintf("postgres://postgres:postgres@localhost:%d/postgres?sslmode=disable", port)
}

func freePort() (int, error) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	defer listener.Close()
	return listener.Addr().(*net.TCPAddr).Port, nil
}

// replaceDatabase swaps the database name in a postgres:// URL.
func replaceDatabase(url, name string) string {
	config, err := pgx.ParseConfig(url)
	if err != nil {
		return url
	}
	sslmode := "disable"
	if config.TLSConfig != nil {
		sslmode = "require"
	}
	return fmt.Sprintf("postgres://%s:%s@%s:%d/%s?sslmode=%s", config.User, config.Password, config.Host, config.Port,
		name, sslmode)
}
