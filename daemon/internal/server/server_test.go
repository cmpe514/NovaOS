package server

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/cmpe514/novaos/daemon/internal/auth"
	"github.com/cmpe514/novaos/daemon/internal/store"
	"github.com/cmpe514/novaos/daemon/internal/system"
	"github.com/cmpe514/novaos/daemon/internal/weather"
)

const origin = "http://127.0.0.1:7171"

type env struct {
	h        http.Handler
	auth     *auth.Manager
	cookie   *http.Cookie
	fake     *system.Fake
	home     string
	pictures string
}

func newEnv(t *testing.T) *env {
	t.Helper()
	ui, home, data := t.TempDir(), t.TempDir(), t.TempDir()
	os.WriteFile(filepath.Join(ui, "index.html"), []byte("<!doctype html><title>NovaOS</title>"), 0o644)
	os.WriteFile(filepath.Join(ui, ".secret"), []byte("x"), 0o644)
	os.Mkdir(filepath.Join(ui, "js"), 0o755)
	os.WriteFile(filepath.Join(ui, "js", "app.js"), []byte("1"), 0o644)
	a, err := auth.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	docs, _ := store.NewDocuments(filepath.Join(home, "Documents"))
	pictures := filepath.Join(home, "Pictures")
	os.Mkdir(pictures, 0o700)
	fake := system.NewFake(false, filepath.Join(home, "usb"))
	h, err := New(Options{
		Listen: "127.0.0.1:7171", UIDir: ui, Auth: a, Records: store.OpenRecords(data, docs), Settings: store.OpenSettings(data),
		Docs: docs, PicturesDir: pictures, System: fake, Weather: weather.New("test"), Name: "NovaOS", Version: "test", Dev: true,
		Log: log.New(io.Discard, "", 0),
	})
	if err != nil {
		t.Fatal(err)
	}
	e := &env{h: h, auth: a, fake: fake, home: home, pictures: pictures}
	res := e.raw(t, "GET", "/auth?code="+a.Code(), nil, map[string]string{"Sec-Fetch-Site": "none"})
	if res.Code != http.StatusSeeOther {
		t.Fatalf("auth: %d", res.Code)
	}
	for _, c := range res.Result().Cookies() {
		if c.Name == auth.CookieName {
			e.cookie = c
		}
	}
	if e.cookie == nil {
		t.Fatal("no session cookie")
	}
	return e
}

func (e *env) raw(t *testing.T, method, path string, body any, headers map[string]string) *httptest.ResponseRecorder {
	t.Helper()
	var r io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		r = bytes.NewReader(b)
	}
	req := httptest.NewRequest(method, "http://127.0.0.1:7171"+path, r)
	req.Host = "127.0.0.1:7171"
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	for k, v := range headers {
		if k == "Host" {
			req.Host = v
			continue
		}
		req.Header.Set(k, v)
	}
	rec := httptest.NewRecorder()
	e.h.ServeHTTP(rec, req)
	if rec.Header().Get("Access-Control-Allow-Origin") != "" {
		t.Fatalf("%s %s sent a CORS header", method, path)
	}
	return rec
}

// api sends a request the way the shell does.
func (e *env) api(t *testing.T, method, path string, body any) (int, map[string]any) {
	t.Helper()
	req := map[string]string{"X-Nova-Request": "1", "Sec-Fetch-Site": "same-origin", "Cookie": e.cookie.Name + "=" + e.cookie.Value}
	if method != "GET" {
		req["Origin"] = origin
	}
	rec := e.raw(t, method, path, body, req)
	var out map[string]any
	json.Unmarshal(rec.Body.Bytes(), &out)
	return rec.Code, out
}

func TestRefusesNonLoopback(t *testing.T) {
	for _, addr := range []string{"0.0.0.0:7171", "192.168.1.5:7171", ":7171", "[::]:7171"} {
		if _, err := New(Options{Listen: addr}); err == nil {
			t.Errorf("%s accepted", addr)
		}
	}
}

func TestRequestGuards(t *testing.T) {
	e := newEnv(t)
	cookie := e.cookie.Name + "=" + e.cookie.Value
	cases := []struct {
		name    string
		method  string
		path    string
		headers map[string]string
		want    int
	}{
		{"DNS rebinding (foreign Host)", "GET", "/", map[string]string{"Host": "evil.example:7171"}, http.StatusMisdirectedRequest},
		{"foreign Host on the API", "GET", "/api/records", map[string]string{"Host": "attacker.test", "Cookie": cookie, "X-Nova-Request": "1"}, http.StatusMisdirectedRequest},
		{"cross-site page", "GET", "/", map[string]string{"Sec-Fetch-Site": "cross-site"}, http.StatusForbidden},
		{"cross-site API call with cookie", "POST", "/api/power", map[string]string{"Sec-Fetch-Site": "cross-site", "Cookie": cookie, "X-Nova-Request": "1"}, http.StatusForbidden},
		{"same-site other port", "GET", "/api/records", map[string]string{"Sec-Fetch-Site": "same-site", "Origin": "http://127.0.0.1:8080", "Cookie": cookie, "X-Nova-Request": "1"}, http.StatusForbidden},
		{"wrong Origin", "POST", "/api/power", map[string]string{"Origin": "https://evil.example", "Cookie": cookie, "X-Nova-Request": "1"}, http.StatusForbidden},
		{"null Origin", "POST", "/api/power", map[string]string{"Origin": "null", "Cookie": cookie, "X-Nova-Request": "1"}, http.StatusForbidden},
		{"no cookie", "GET", "/api/records", map[string]string{"X-Nova-Request": "1"}, http.StatusUnauthorized},
		{"bad cookie", "GET", "/api/records", map[string]string{"Cookie": auth.CookieName + "=nope", "X-Nova-Request": "1"}, http.StatusUnauthorized},
		{"no custom header (simple form POST)", "POST", "/api/power", map[string]string{"Cookie": cookie}, http.StatusForbidden},
		{"CORS preflight", "OPTIONS", "/api/power", map[string]string{"Origin": "https://evil.example", "Access-Control-Request-Method": "POST"}, http.StatusForbidden},
		{"auth from another site", "GET", "/auth?code=" + e.auth.Code(), map[string]string{"Sec-Fetch-Site": "cross-site"}, http.StatusForbidden},
		{"auth with a wrong code", "GET", "/auth?code=0000", map[string]string{"Sec-Fetch-Site": "none"}, http.StatusForbidden},
		{"dotfile", "GET", "/.secret", nil, http.StatusNotFound},
		{"directory listing", "GET", "/js/", nil, http.StatusNotFound},
		{"static POST", "POST", "/index.html", nil, http.StatusMethodNotAllowed},
		{"healthz needs nothing", "GET", "/healthz", nil, http.StatusOK},
		{"UI loads without a session", "GET", "/", nil, http.StatusOK},
	}
	for _, c := range cases {
		if got := e.raw(t, c.method, c.path, nil, c.headers).Code; got != c.want {
			t.Errorf("%s: got %d, want %d", c.name, got, c.want)
		}
	}
	if len(e.fake.PowerLog) != 0 {
		t.Fatal("a rejected request reached the power API")
	}
	res := e.raw(t, "GET", "/", nil, nil)
	for _, h := range []string{"Content-Security-Policy", "X-Content-Type-Options", "Cross-Origin-Resource-Policy", "X-Frame-Options"} {
		if res.Header().Get(h) == "" {
			t.Errorf("missing %s", h)
		}
	}
	if !strings.Contains(res.Header().Get("Content-Security-Policy"), "script-src 'self'") {
		t.Error("CSP must restrict scripts to self")
	}
}

func TestLoginCodeIsSingleUse(t *testing.T) {
	e := newEnv(t)
	code := e.auth.Code()
	if got := e.raw(t, "GET", "/auth?code="+code, nil, nil).Code; got != http.StatusSeeOther {
		t.Fatalf("fresh code: %d", got)
	}
	if got := e.raw(t, "GET", "/auth?code="+code, nil, nil).Code; got != http.StatusForbidden {
		t.Fatalf("reused code: %d", got)
	}
}

func TestRecordsAPI(t *testing.T) {
	e := newEnv(t)
	code, out := e.api(t, "POST", "/api/records", map[string]any{"app_type": "task", "title": "Buy milk", "completed": false, "created_at": "2026-03-14T09:41:00.000Z"})
	if code != 201 {
		t.Fatalf("create %d %v", code, out)
	}
	id := out["record"].(map[string]any)["__backendId"].(string)
	code, _ = e.api(t, "PUT", "/api/records/"+id, map[string]any{"app_type": "task", "title": "Buy milk", "completed": true})
	if code != 200 {
		t.Fatalf("update %d", code)
	}
	code, out = e.api(t, "POST", "/api/records", map[string]any{"app_type": "file", "title": "plan.txt", "content": "step 1"})
	if code != 201 || out["record"].(map[string]any)["__backendId"] != "doc:plan.txt" {
		t.Fatalf("file create %d %v", code, out)
	}
	if b, _ := os.ReadFile(filepath.Join(e.home, "Documents", "plan.txt")); string(b) != "step 1" {
		t.Fatalf("Documents/plan.txt = %q", b)
	}
	code, out = e.api(t, "GET", "/api/records", nil)
	recs := out["records"].([]any)
	if code != 200 || len(recs) != 2 {
		t.Fatalf("list %d %v", code, out)
	}
	code, out = e.api(t, "POST", "/api/records", map[string]any{"app_type": "file", "title": "../escape.txt"})
	if code != 400 || out["error"] == nil {
		t.Fatalf("traversal must be refused: %d %v", code, out)
	}
	if code, _ := e.api(t, "DELETE", "/api/records/doc:plan.txt", nil); code != 200 {
		t.Fatalf("delete file %d", code)
	}
	if code, _ := e.api(t, "DELETE", "/api/records/"+id, nil); code != 200 {
		t.Fatalf("delete %d", code)
	}
	if code, _ := e.api(t, "DELETE", "/api/records/"+id, nil); code != 404 {
		t.Fatalf("delete again %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/records", map[string]any{"app_type": "virus", "title": "x"}); code != 400 {
		t.Fatalf("bad type %d", code)
	}
}

func TestSettingsAndSession(t *testing.T) {
	e := newEnv(t)
	code, out := e.api(t, "PUT", "/api/settings", map[string]any{"settings": map[string]any{"alt": true, "accent": "blue", "scale": 999, "rm": "-rf"}})
	if code != 200 {
		t.Fatalf("%d %v", code, out)
	}
	_, out = e.api(t, "GET", "/api/settings", nil)
	s := out["settings"].(map[string]any)
	if s["alt"] != true || s["accent"] != "blue" || s["scale"] != 120.0 || s["rm"] != nil {
		t.Fatalf("settings %v", s)
	}
	_, out = e.api(t, "GET", "/api/session", nil)
	if out["name"] != "NovaOS" || out["dev"] != true || out["features"].(map[string]any)["documents"] != true {
		t.Fatalf("session %v", out)
	}
}

func TestSystemAPI(t *testing.T) {
	e := newEnv(t)
	if code, out := e.api(t, "POST", "/api/power", map[string]any{"action": "poweroff"}); code != 200 || out["dryRun"] != true {
		t.Fatalf("power %d %v", code, out)
	}
	if code, _ := e.api(t, "POST", "/api/power", map[string]any{"action": "explode"}); code != 400 {
		t.Fatalf("bad power action %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/browser/open", map[string]any{"url": "javascript:alert(1)"}); code != 400 {
		t.Fatalf("javascript: URL %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/browser/open", map[string]any{"url": "https://example.com/"}); code != 200 {
		t.Fatalf("open %d", code)
	}
	if len(e.fake.Opened) != 1 || e.fake.Opened[0] != "https://example.com/" {
		t.Fatalf("opened %v", e.fake.Opened)
	}
	if code, _ := e.api(t, "POST", "/api/wifi", map[string]any{}); code != 400 {
		t.Fatalf("wifi without enabled %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/wifi", map[string]any{"enabled": false}); code != 200 {
		t.Fatalf("wifi off %d", code)
	}
	_, st := e.api(t, "GET", "/api/system/status", nil)
	if st["wifi"].(map[string]any)["enabled"] != false || st["network"].(map[string]any)["state"] != "disconnected" {
		t.Fatalf("status after wifi off %v", st)
	}
	if code, _ := e.api(t, "POST", "/api/bluetooth/selfdestruct", map[string]any{"address": "AA:BB:CC:00:11:22"}); code != 404 {
		t.Fatalf("unknown bluetooth action %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/storage/setup", map[string]any{"device": "vdb", "confirm": "vd"}); code != 400 {
		t.Fatalf("storage without exact confirmation %d", code)
	}
	if code, _ := e.api(t, "POST", "/api/storage/setup", map[string]any{"device": "vda", "confirm": "vda"}); code != 400 {
		t.Fatalf("storage on a non-blank disk %d", code)
	}
	if len(e.fake.SetupLog) != 0 {
		t.Fatal("storage setup ran without valid confirmation")
	}
	if code, _ := e.api(t, "POST", "/api/storage/setup", map[string]any{"device": "vdb", "confirm": "vdb"}); code != 200 || len(e.fake.SetupLog) != 1 {
		t.Fatalf("storage setup %d %v", code, e.fake.SetupLog)
	}
	if code, out := e.api(t, "GET", "/api/weather", nil); code != 200 || out["live"] != false {
		t.Fatalf("weather without a place must be a labelled sample: %d %v", code, out)
	}
}

func TestUSBImportOnlyListedFiles(t *testing.T) {
	e := newEnv(t)
	if code, _ := e.api(t, "POST", "/api/usb/mount", map[string]any{"id": "sdb1"}); code != 200 {
		t.Fatal("mount")
	}
	outside := filepath.Join(e.home, "outside.txt")
	os.WriteFile(outside, []byte("secret"), 0o600)
	os.Symlink(outside, filepath.Join(e.home, "usb", "link.txt"))
	_, out := e.api(t, "GET", "/api/usb/files?id=sdb1", nil)
	files := out["files"].([]any)
	if len(files) != 1 || files[0].(map[string]any)["path"] != "readme.txt" {
		t.Fatalf("files %v (symlinks must not be listed)", files)
	}
	for _, p := range []string{"../outside.txt", "link.txt", "/etc/passwd"} {
		if code, _ := e.api(t, "POST", "/api/usb/import", map[string]any{"id": "sdb1", "path": p}); code != 400 {
			t.Errorf("import %q: %d", p, code)
		}
	}
	if code, _ := e.api(t, "POST", "/api/usb/import", map[string]any{"id": "sdb1", "path": "readme.txt"}); code != 201 {
		t.Fatalf("import readme %d", code)
	}
	if b, _ := os.ReadFile(filepath.Join(e.home, "Documents", "readme.txt")); !strings.Contains(string(b), "USB stick") {
		t.Fatalf("imported %q", b)
	}
}

func TestPhotos(t *testing.T) {
	e := newEnv(t)
	os.WriteFile(filepath.Join(e.pictures, "beach.jpg"), []byte("\xff\xd8\xff fake jpeg"), 0o600)
	os.WriteFile(filepath.Join(e.pictures, "notes.txt"), []byte("not a photo"), 0o600)
	os.WriteFile(filepath.Join(e.home, "private.jpg"), []byte("private"), 0o600)
	_, out := e.api(t, "GET", "/api/photos", nil)
	photos := out["photos"].([]any)
	if len(photos) != 1 || photos[0].(map[string]any)["url"] != "/api/photos/file?name=beach.jpg" {
		t.Fatalf("photos %v", photos)
	}
	cookie := map[string]string{"Cookie": e.cookie.Name + "=" + e.cookie.Value, "Sec-Fetch-Site": "same-origin"}
	res := e.raw(t, "GET", "/api/photos/file?name=beach.jpg", nil, cookie)
	if res.Code != 200 || res.Header().Get("Content-Type") != "image/jpeg" {
		t.Fatalf("photo %d %s", res.Code, res.Header().Get("Content-Type"))
	}
	for _, name := range []string{"../private.jpg", "notes.txt", "%2e%2e/private.jpg"} {
		if code := e.raw(t, "GET", "/api/photos/file?name="+name, nil, cookie).Code; code != 404 {
			t.Errorf("%s: %d", name, code)
		}
	}
	if code := e.raw(t, "GET", "/api/photos/file?name=beach.jpg", nil, nil).Code; code != 401 {
		t.Errorf("photo without session: %d", code)
	}
}
