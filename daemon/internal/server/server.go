// Package server is novad's HTTP surface: the shell UI (static files), the data API behind the
// dataSdk, the system API and /healthz — all on 127.0.0.1 only.
//
// Request checks, outermost first:
//  1. Host must be 127.0.0.1:<port> or localhost:<port>        (defeats DNS rebinding)
//  2. Sec-Fetch-Site "cross-site" is refused everywhere           (no use from other sites)
//  3. /api/*: Origin, when sent, must be this exact origin; Sec-Fetch-Site, when sent, must be
//     "same-origin"; the session cookie must be valid; and every request except image GETs must carry
//     X-Nova-Request: 1, which no cross-origin page can send without a CORS preflight — and preflights
//     are answered without any Access-Control-* header, so they fail.
//
// The Browser app runs in a separate Chromium profile that never receives the session cookie.
package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"runtime/debug"
	"strings"
	"sync"
	"time"

	"github.com/cmpe514/novaos/daemon/internal/auth"
	"github.com/cmpe514/novaos/daemon/internal/store"
	"github.com/cmpe514/novaos/daemon/internal/system"
	"github.com/cmpe514/novaos/daemon/internal/weather"
)

// Options wires the server together.
type Options struct {
	Listen      string // 127.0.0.1:7171
	UIDir       string
	DataDir     string // records.json, settings.json, boots.json
	Auth        *auth.Manager
	Records     *store.Records
	Settings    *store.Settings
	Docs        *store.Documents // nil: files are stored as records
	PicturesDir string
	System      system.System
	Weather     *weather.Client
	Name        string
	Version     string
	Base        string // e.g. "Debian GNU/Linux 13 (trixie)"
	Dev         bool
	Log         *log.Logger
	// ReloadStores is called after a new data disk was set up and mounted.
	ReloadStores func()
}

type server struct {
	Options
	origins   map[string]bool
	hosts     map[string]bool
	readyOnce sync.Once
}

const csp = "default-src 'self'; script-src 'self'; style-src 'self'; style-src-attr 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self'; media-src 'self' blob:; object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"

// New builds the handler. Listen must be a loopback address.
func New(o Options) (http.Handler, error) {
	host, port, err := net.SplitHostPort(o.Listen)
	if err != nil {
		return nil, fmt.Errorf("listen address: %w", err)
	}
	if ip := net.ParseIP(host); ip == nil || !ip.IsLoopback() {
		return nil, fmt.Errorf("refusing to listen on %q: novad only listens on 127.0.0.1", o.Listen)
	}
	if o.Log == nil {
		o.Log = log.New(os.Stderr, "novad ", log.LstdFlags)
	}
	s := &server{
		Options: o,
		hosts:   map[string]bool{"127.0.0.1:" + port: true, "localhost:" + port: true},
		origins: map[string]bool{"http://127.0.0.1:" + port: true, "http://localhost:" + port: true},
	}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", s.healthz)
	mux.HandleFunc("GET /auth", s.authenticate)
	mux.Handle("/api/", s.api())
	mux.Handle("/", s.static())
	return s.outer(mux), nil
}

// ---------- outer middleware ----------

type recorder struct {
	http.ResponseWriter
	status int
}

func (r *recorder) WriteHeader(code int) { r.status = code; r.ResponseWriter.WriteHeader(code) }

func (s *server) outer(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		rec := &recorder{ResponseWriter: w, status: 200}
		defer func() {
			if p := recover(); p != nil {
				s.Log.Printf("panic: %v\n%s", p, debug.Stack())
				http.Error(rec, "internal error", http.StatusInternalServerError)
			}
			// Never log query strings: /auth carries the login code.
			s.Log.Printf("%s %s %d %s", r.Method, r.URL.Path, rec.status, time.Since(start).Round(time.Millisecond))
		}()
		h := w.Header()
		h.Set("Content-Security-Policy", csp)
		h.Set("X-Content-Type-Options", "nosniff")
		h.Set("Referrer-Policy", "no-referrer")
		h.Set("Cross-Origin-Opener-Policy", "same-origin")
		h.Set("Cross-Origin-Resource-Policy", "same-origin")
		h.Set("X-Frame-Options", "DENY")
		h.Set("Permissions-Policy", "camera=(), microphone=(), geolocation=(), usb=(), serial=(), hid=(), bluetooth=()")
		if !s.hosts[r.Host] {
			http.Error(rec, "misdirected request", http.StatusMisdirectedRequest)
			return
		}
		if r.Header.Get("Sec-Fetch-Site") == "cross-site" {
			http.Error(rec, "cross-site requests are not allowed", http.StatusForbidden)
			return
		}
		if r.Method == http.MethodOptions {
			// No CORS, ever: a preflight gets no Access-Control-* headers and therefore fails.
			http.Error(rec, "CORS is not supported", http.StatusForbidden)
			return
		}
		next.ServeHTTP(rec, r)
	})
}

func (s *server) healthz(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "name": s.Name, "version": s.Version})
}

func (s *server) authenticate(w http.ResponseWriter, r *http.Request) {
	if site := r.Header.Get("Sec-Fetch-Site"); site != "" && site != "none" && site != "same-origin" {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	cookie, ok := s.Auth.Redeem(r.URL.Query().Get("code"))
	if !ok {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		w.WriteHeader(http.StatusForbidden)
		io.WriteString(w, "This sign-in link has expired. Restart the session to get a new one.\n")
		return
	}
	http.SetCookie(w, cookie)
	w.Header().Set("Cache-Control", "no-store")
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

// ---------- static UI ----------

func (s *server) static() http.Handler {
	files := http.FileServer(http.Dir(s.UIDir))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		p := r.URL.Path
		for _, part := range strings.Split(p, "/") {
			if strings.HasPrefix(part, ".") {
				http.NotFound(w, r)
				return
			}
		}
		if p != "/" && strings.HasSuffix(p, "/") {
			http.NotFound(w, r) // no directory listings
			return
		}
		if p == "/" || strings.HasSuffix(p, ".html") {
			w.Header().Set("Cache-Control", "no-cache")
		}
		files.ServeHTTP(w, r)
	})
}

// ---------- helpers ----------

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func readJSON(r *http.Request, v any) error {
	if ct := r.Header.Get("Content-Type"); !strings.HasPrefix(ct, "application/json") {
		return &store.ValidationError{Msg: "expected a JSON body"}
	}
	dec := json.NewDecoder(io.LimitReader(r.Body, 1<<20))
	if err := dec.Decode(v); err != nil {
		return &store.ValidationError{Msg: "invalid JSON body"}
	}
	return nil
}

// fail maps errors to status codes; only user-safe messages are sent to the client.
func (s *server) fail(w http.ResponseWriter, r *http.Request, err error) {
	var verr *store.ValidationError
	var uerr *system.UserError
	switch {
	case errors.As(err, &verr):
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": verr.Msg})
	case errors.As(err, &uerr):
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": uerr.Msg})
	case errors.Is(err, store.ErrNotFound):
		writeJSON(w, http.StatusNotFound, map[string]string{"error": err.Error()})
	case errors.Is(err, store.ErrFull):
		writeJSON(w, http.StatusInsufficientStorage, map[string]string{"error": err.Error()})
	case errors.Is(err, system.ErrUnsupported):
		writeJSON(w, http.StatusNotImplemented, map[string]string{"error": err.Error()})
	case errors.Is(err, context.DeadlineExceeded):
		writeJSON(w, http.StatusGatewayTimeout, map[string]string{"error": "the device took too long to answer"})
	default:
		s.Log.Printf("%s %s: %v", r.Method, r.URL.Path, err)
		msg := "something went wrong on the device"
		if s.Records.LoadError() != nil || s.Settings.LoadError() != nil {
			msg = err.Error() // damaged data files: say so plainly, never pretend
		}
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": msg})
	}
}

func cleanJoin(dir, name string) (string, error) {
	if err := store.ValidName(name); err != nil {
		return "", err
	}
	p := filepath.Join(dir, name)
	if filepath.Dir(p) != filepath.Clean(dir) {
		return "", &store.ValidationError{Msg: "invalid name"}
	}
	return p, nil
}
