package auth

import (
	"net/http/httptest"
	"os"
	"strings"
	"testing"
)

func TestRedeemRotatesCode(t *testing.T) {
	dir := t.TempDir()
	m, err := New(dir + "/nova")
	if err != nil {
		t.Fatal(err)
	}
	info, _ := os.Stat(m.CodePath())
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("login-code must be 0600, is %v", info.Mode().Perm())
	}
	dirInfo, _ := os.Stat(dir + "/nova")
	if dirInfo.Mode().Perm() != 0o700 {
		t.Fatalf("runtime dir must be 0700, is %v", dirInfo.Mode().Perm())
	}
	data, _ := os.ReadFile(m.CodePath())
	code := strings.TrimSpace(string(data))
	if code != m.Code() || len(code) != 64 {
		t.Fatalf("code file %q", code)
	}
	if _, ok := m.Redeem("wrong"); ok {
		t.Fatal("wrong code accepted")
	}
	if _, ok := m.Redeem(""); ok {
		t.Fatal("empty code accepted")
	}
	cookie, ok := m.Redeem(code)
	if !ok || !cookie.HttpOnly || cookie.SameSite != 3 /* Strict */ {
		t.Fatalf("bad cookie %+v", cookie)
	}
	if _, ok := m.Redeem(code); ok {
		t.Fatal("a code must work only once")
	}
	data, _ = os.ReadFile(m.CodePath())
	if strings.TrimSpace(string(data)) == code {
		t.Fatal("code file not rotated")
	}
	req := httptest.NewRequest("GET", "/api/session", nil)
	if m.Valid(req) {
		t.Fatal("no cookie must be invalid")
	}
	req.AddCookie(cookie)
	if !m.Valid(req) {
		t.Fatal("cookie rejected")
	}
	other, _ := New(t.TempDir())
	if other.Valid(req) {
		t.Fatal("cookie from another boot accepted")
	}
}
