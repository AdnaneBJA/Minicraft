// Package web is the public side of the stats service: the dashboard pages and the JSON API they poll.
package web

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"html/template"
	"io/fs"
	"log"
	"net/http"
	"path/filepath"
	"strings"
	"time"

	"github.com/AdnaneBJA/Minicraft/stats/internal/store"
)

//go:embed templates/*.html
var templateFiles embed.FS

//go:embed static
var staticFiles embed.FS

// Handler serves everything under /stats. `online` reports the players in the world right now; `spritesDir` holds
// the game's sprite sheets (items.png, zombie.png, ...) for the icons.
func Handler(st *store.Store, online func() int, spritesDir string) http.Handler {
	site := &site{store: st, online: online, pages: parseTemplates()}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /stats", site.dashboard)
	mux.HandleFunc("GET /stats/{$}", site.dashboard)
	mux.HandleFunc("GET /stats/player/{name}", site.playerPage)
	mux.HandleFunc("GET /stats/api/summary", site.api(func(ctx context.Context) (any, error) { return site.summary(ctx) }))
	mux.HandleFunc("GET /stats/api/leaderboards", site.api(func(ctx context.Context) (any, error) { return st.Leaderboards(ctx) }))
	mux.HandleFunc("GET /stats/api/items", site.api(func(ctx context.Context) (any, error) { return st.Items(ctx) }))
	mux.HandleFunc("GET /stats/api/mobs", site.api(func(ctx context.Context) (any, error) { return st.Mobs(ctx) }))
	mux.HandleFunc("GET /stats/api/tiles", site.api(func(ctx context.Context) (any, error) { return st.Tiles(ctx) }))
	mux.HandleFunc("GET /stats/api/timeline", site.api(func(ctx context.Context) (any, error) { return st.Timeline(ctx) }))
	mux.HandleFunc("GET /stats/api/recent", site.api(func(ctx context.Context) (any, error) { return st.Recent(ctx) }))
	mux.HandleFunc("GET /stats/api/player/{name}", site.playerAPI)
	static, _ := fs.Sub(staticFiles, "static")
	mux.Handle("GET /stats/static/", http.StripPrefix("/stats/static/", http.FileServerFS(static)))
	mux.Handle("GET /stats/sprites/{file}", sprites(spritesDir))
	return mux
}

type site struct {
	store  *store.Store
	online func() int
	pages  map[string]*template.Template
}

func (s *site) summary(ctx context.Context) (store.Summary, error) {
	sum, err := s.store.Summary(ctx)
	sum.Online = s.online()
	return sum, err
}

func (s *site) api(read func(context.Context) (any, error)) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		value, err := read(r.Context())
		if err != nil {
			serverError(w, err)
			return
		}
		writeJSON(w, value)
	}
}

func (s *site) playerAPI(w http.ResponseWriter, r *http.Request) {
	card, err := s.store.Player(r.Context(), r.PathValue("name"))
	if errors.Is(err, store.ErrNotFound) {
		http.Error(w, "no such player", http.StatusNotFound)
		return
	}
	if err != nil {
		serverError(w, err)
		return
	}
	writeJSON(w, card)
}

// dashboardData is everything the dashboard shows when it first loads (the script then keeps it fresh).
type dashboardData struct {
	Summary      store.Summary
	Leaderboards store.Leaderboards
	Items        []store.ItemStat
	Mobs         []store.MobStat
	Recent       []store.RecentEvent
	Deadliest    *store.MobStat
}

func (s *site) dashboard(w http.ResponseWriter, r *http.Request) {
	ctx := r.Context()
	var data dashboardData
	var err error
	if data.Summary, err = s.summary(ctx); err != nil {
		serverError(w, err)
		return
	}
	if data.Leaderboards, err = s.store.Leaderboards(ctx); err != nil {
		serverError(w, err)
		return
	}
	if data.Items, err = s.store.Items(ctx); err != nil {
		serverError(w, err)
		return
	}
	if data.Mobs, err = s.store.Mobs(ctx); err != nil {
		serverError(w, err)
		return
	}
	if data.Recent, err = s.store.Recent(ctx); err != nil {
		serverError(w, err)
		return
	}
	for i := range data.Mobs {
		if data.Mobs[i].PlayersKilled > 0 && (data.Deadliest == nil || data.Mobs[i].PlayersKilled > data.Deadliest.PlayersKilled) {
			data.Deadliest = &data.Mobs[i]
		}
	}
	s.render(w, "index.html", data)
}

func (s *site) playerPage(w http.ResponseWriter, r *http.Request) {
	card, err := s.store.Player(r.Context(), r.PathValue("name"))
	if errors.Is(err, store.ErrNotFound) {
		w.WriteHeader(http.StatusNotFound)
		s.render(w, "missing.html", r.PathValue("name"))
		return
	}
	if err != nil {
		serverError(w, err)
		return
	}
	s.render(w, "player.html", card)
}

func (s *site) render(w http.ResponseWriter, page string, data any) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := s.pages[page].ExecuteTemplate(w, "layout", data); err != nil {
		log.Printf("rendering %s: %v", page, err)
	}
}

// sprites serves the game's PNG sprite sheets, and nothing else from that folder.
func sprites(dir string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		file := r.PathValue("file")
		if file != filepath.Base(file) || !strings.HasSuffix(file, ".png") || strings.HasPrefix(file, ".") {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Cache-Control", "public, max-age=86400")
		http.ServeFile(w, r, filepath.Join(dir, file))
	})
}

func writeJSON(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	_ = json.NewEncoder(w).Encode(value)
}

func serverError(w http.ResponseWriter, err error) {
	log.Printf("stats: %v", err)
	http.Error(w, "stats are unavailable right now", http.StatusServiceUnavailable)
}

func parseTemplates() map[string]*template.Template {
	funcs := template.FuncMap{
		"num":      func(v any) string { return formatNumber(toInt64(v)) },
		"duration": func(v any) string { return formatDuration(toInt64(v)) },
		"ago":      formatAgo,
		"depth":    func(v any) string { return depthName(toInt64(v)) },
		"mobFile":  mobSprite,
		"iconX":    func(icon int) int { return icon * 8 },
		"inc":      func(i int) int { return i + 1 },
		"percent": func(part, whole int64) float64 {
			if whole <= 0 {
				return 0
			}
			return 100 * float64(part) / float64(whole)
		},
		// board bundles one leaderboard's title, API key, lines and how to show the values.
		"board": func(title, key string, leaders []store.Leader, format string) map[string]any {
			return map[string]any{"Title": title, "Key": key, "Leaders": leaders, "Format": format}
		},
		"fmtValue": func(format string, v int64) string {
			switch format {
			case "duration":
				return formatDuration(v)
			case "depth":
				return depthName(v)
			}
			return formatNumber(v)
		},
		"json": func(v any) template.JS {
			b, _ := json.Marshal(v)
			return template.JS(b)
		},
	}
	pages := map[string]*template.Template{}
	for _, page := range []string{"index.html", "player.html", "missing.html"} {
		pages[page] = template.Must(template.New("").Funcs(funcs).ParseFS(templateFiles, "templates/layout.html",
			"templates/"+page))
	}
	return pages
}

// toInt64 lets the template helpers take any integer field.
func toInt64(v any) int64 {
	switch n := v.(type) {
	case int:
		return int64(n)
	case int64:
		return n
	case int32:
		return int64(n)
	}
	return 0
}

// formatNumber: 12345 -> "12,345".
func formatNumber(n int64) string {
	s := fmt.Sprint(n)
	if n < 0 {
		return "-" + formatNumber(-n)
	}
	for i := len(s) - 3; i > 0; i -= 3 {
		s = s[:i] + "," + s[i:]
	}
	return s
}

// formatDuration: seconds as "2h 05m", "4m 10s" or "12s".
func formatDuration(seconds int64) string {
	switch {
	case seconds >= 3600:
		return fmt.Sprintf("%dh %02dm", seconds/3600, seconds/60%60)
	case seconds >= 60:
		return fmt.Sprintf("%dm %02ds", seconds/60, seconds%60)
	default:
		return fmt.Sprintf("%ds", seconds)
	}
}

func formatAgo(t time.Time) string {
	d := time.Since(t)
	switch {
	case d < time.Minute:
		return "just now"
	case d < time.Hour:
		return fmt.Sprintf("%d min ago", int(d.Minutes()))
	case d < 24*time.Hour:
		return fmt.Sprintf("%d h ago", int(d.Hours()))
	default:
		return fmt.Sprintf("%d d ago", int(d.Hours()/24))
	}
}

func depthName(depth int64) string {
	switch {
	case depth >= 4:
		return "The sky"
	case depth >= 1:
		return fmt.Sprintf("Cave %d", depth)
	default:
		return "Surface"
	}
}

// mobSprite: "Air Wizard" -> "air_wizard.png".
func mobSprite(mob string) string {
	return strings.ReplaceAll(strings.ToLower(mob), " ", "_") + ".png"
}
