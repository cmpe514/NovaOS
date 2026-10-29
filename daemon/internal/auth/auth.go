// Package auth implements novad's per-boot session.
//
// At start-up novad creates two random secrets that only live in memory and in the runtime directory:
//   - a session secret, handed to the shell as an HttpOnly, SameSite=Strict cookie;
//   - a one-time login code, written to <runtime>/login-code (mode 0600, readable only by the session
//     user). The shell launcher opens /auth?code=<code>; novad swaps it for the cookie and immediately
//     writes a fresh code, so a code seen once (for example in a process list) is useless afterwards.
//
// The Browser app runs Chromium with a different profile, so web pages never hold the cookie; and the
// cookie is SameSite=Strict, so even in the same profile no cross-site request would carry it.
package auth

import (
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"net/http"
	"os"
	"path/filepath"
	"sync"
)

// CookieName is the session cookie.
const CookieName = "nova_session"

// Manager holds the per-boot secrets.
type Manager struct {
	mu       sync.Mutex
	secret   string
	code     string
	codePath string
}

func random() string {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

// New creates fresh secrets and writes the first login code into runtimeDir (created 0700).
func New(runtimeDir string) (*Manager, error) {
	if err := os.MkdirAll(runtimeDir, 0o700); err != nil {
		return nil, err
	}
	if err := os.Chmod(runtimeDir, 0o700); err != nil {
		return nil, err
	}
	m := &Manager{secret: random(), codePath: filepath.Join(runtimeDir, "login-code")}
	if err := m.rotate(); err != nil {
		return nil, err
	}
	return m, nil
}

// CodePath is where the current one-time login code is kept.
func (m *Manager) CodePath() string { return m.codePath }

// Code returns the current one-time login code.
func (m *Manager) Code() string {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.code
}

func (m *Manager) rotate() error {
	code := random()
	tmp := m.codePath + ".tmp"
	if err := os.WriteFile(tmp, []byte(code+"\n"), 0o600); err != nil {
		return err
	}
	if err := os.Rename(tmp, m.codePath); err != nil {
		return err
	}
	m.code = code
	return nil
}

func equal(a, b string) bool { return subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1 }

// Redeem exchanges a valid one-time code for the session cookie and rotates the code.
func (m *Manager) Redeem(code string) (*http.Cookie, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if code == "" || !equal(code, m.code) {
		return nil, false
	}
	if err := m.rotate(); err != nil {
		return nil, false
	}
	return &http.Cookie{Name: CookieName, Value: m.secret, Path: "/", HttpOnly: true, SameSite: http.SameSiteStrictMode}, true
}

// Valid reports whether the request carries this boot's session cookie.
func (m *Manager) Valid(r *http.Request) bool {
	c, err := r.Cookie(CookieName)
	return err == nil && equal(c.Value, m.secret)
}
