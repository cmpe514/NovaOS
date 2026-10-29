package server

import (
	"context"
	"io/fs"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/cmpe514/novaos/daemon/internal/store"
	"github.com/cmpe514/novaos/daemon/internal/system"
)

// api returns the /api/* handler with its guard.
func (s *server) api() http.Handler {
	mux := http.NewServeMux()
	route := func(pattern string, timeout time.Duration, h func(http.ResponseWriter, *http.Request)) {
		mux.HandleFunc(pattern, func(w http.ResponseWriter, r *http.Request) {
			ctx, cancel := context.WithTimeout(r.Context(), timeout)
			defer cancel()
			h(w, r.WithContext(ctx))
		})
	}
	const quick, slow = 15 * time.Second, 45 * time.Second

	route("GET /api/session", quick, s.session)
	route("POST /api/ready", quick, s.ready)
	route("GET /api/settings", quick, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, 200, map[string]any{"settings": s.Settings.UI()})
	})
	route("PUT /api/settings", quick, s.putSettings)

	route("GET /api/records", quick, s.listRecords)
	route("POST /api/records", quick, s.createRecord)
	route("PUT /api/records/{id}", quick, s.updateRecord)
	route("DELETE /api/records/{id}", quick, s.deleteRecord)

	route("GET /api/system/status", quick, s.status)
	route("POST /api/power", quick, s.power)
	route("POST /api/browser/open", quick, s.openURL)
	route("POST /api/wifi", quick, s.setWifi)
	route("GET /api/wifi/networks", slow, s.wifiNetworks)
	route("POST /api/wifi/connect", slow, s.wifiConnect)
	route("POST /api/wifi/disconnect", quick, func(w http.ResponseWriter, r *http.Request) { s.done(w, r, s.System.WifiDisconnect(r.Context())) })
	route("POST /api/bluetooth", quick, s.setBluetooth)
	route("GET /api/bluetooth/devices", slow, s.bluetoothDevices)
	route("POST /api/bluetooth/{action}", slow, s.bluetoothAction)
	route("POST /api/audio", quick, s.audio)
	route("POST /api/backlight", quick, s.backlight)
	route("GET /api/timezones", quick, s.timezones)
	route("POST /api/timezone", quick, s.setTimezone)
	route("GET /api/weather", quick, s.weather)
	route("POST /api/weather/place", quick, s.weatherPlace)
	route("GET /api/storage/candidates", quick, s.storageCandidates)
	route("POST /api/storage/setup", 6*time.Minute, s.storageSetup)
	route("POST /api/usb/mount", slow, s.usbMount)
	route("POST /api/usb/unmount", slow, s.usbUnmount)
	route("GET /api/usb/files", quick, s.usbFiles)
	route("POST /api/usb/import", quick, s.usbImport)
	route("GET /api/photos", quick, s.photos)
	route("GET /api/photos/file", quick, s.photoFile)
	if fake, ok := s.System.(*system.Fake); ok && s.Dev {
		route("GET /api/dev/state", quick, func(w http.ResponseWriter, r *http.Request) {
			opened, power, setup := fake.Log()
			writeJSON(w, 200, map[string]any{"opened": opened, "power": power, "setup": setup})
		})
	}

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if o := r.Header.Get("Origin"); o != "" && !s.origins[o] {
			writeJSON(w, http.StatusForbidden, map[string]string{"error": "origin not allowed"})
			return
		}
		if site := r.Header.Get("Sec-Fetch-Site"); site != "" && site != "same-origin" {
			writeJSON(w, http.StatusForbidden, map[string]string{"error": "only the shell may use this API"})
			return
		}
		if !s.Auth.Valid(r) {
			writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "not signed in"})
			return
		}
		imageGET := r.Method == http.MethodGet && r.URL.Path == "/api/photos/file"
		if !imageGET && r.Header.Get("X-Nova-Request") != "1" {
			writeJSON(w, http.StatusForbidden, map[string]string{"error": "missing X-Nova-Request header"})
			return
		}
		mux.ServeHTTP(w, r)
	})
}

func (s *server) done(w http.ResponseWriter, r *http.Request, err error) {
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// ---------- session, settings, records ----------

func (s *server) session(w http.ResponseWriter, r *http.Request) {
	st, _ := s.System.Status(r.Context())
	writeJSON(w, 200, map[string]any{
		"name": s.Name, "version": s.Version, "base": s.Base, "dev": s.Dev,
		"persistent": st.Storage.Persistent,
		"features":   map[string]bool{"documents": s.Docs != nil},
	})
}

// ready is called once the shell has rendered. The NOVAOS_READY line is what the QEMU smoke test and
// boot-time measurements wait for (novaos-ready-marker.service copies it to the serial console).
func (s *server) ready(w http.ResponseWriter, r *http.Request) {
	s.readyOnce.Do(func() {
		uptime := "?"
		if b, err := os.ReadFile("/proc/uptime"); err == nil {
			uptime = strings.Fields(string(b))[0]
		}
		s.Log.Printf("NOVAOS_READY shell rendered %s s after boot", uptime)
	})
	writeJSON(w, 200, map[string]bool{"ok": true})
}

func (s *server) putSettings(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Settings map[string]any `json:"settings"`
	}
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	out, err := s.Settings.SetUI(body.Settings)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 200, map[string]any{"settings": out})
}

func (s *server) listRecords(w http.ResponseWriter, r *http.Request) {
	recs, err := s.Records.List()
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if recs == nil {
		recs = []store.Record{}
	}
	writeJSON(w, 200, map[string]any{"records": recs})
}

func (s *server) createRecord(w http.ResponseWriter, r *http.Request) {
	var in store.Record
	if err := readJSON(r, &in); err != nil {
		s.fail(w, r, err)
		return
	}
	rec, err := s.Records.Create(in)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 201, map[string]any{"record": rec})
}

func (s *server) updateRecord(w http.ResponseWriter, r *http.Request) {
	var in store.Record
	if err := readJSON(r, &in); err != nil {
		s.fail(w, r, err)
		return
	}
	rec, err := s.Records.Update(r.PathValue("id"), in)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 200, map[string]any{"record": rec})
}

func (s *server) deleteRecord(w http.ResponseWriter, r *http.Request) {
	s.done(w, r, s.Records.Delete(r.PathValue("id")))
}

// ---------- system ----------

func (s *server) status(w http.ResponseWriter, r *http.Request) {
	st, err := s.System.Status(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if st.USB == nil {
		st.USB = []system.USBDrive{}
	}
	writeJSON(w, 200, st)
}

func (s *server) power(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Action string `json:"action"`
	}
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	if body.Action != "poweroff" && body.Action != "reboot" {
		s.fail(w, r, &store.ValidationError{Msg: "action must be poweroff or reboot"})
		return
	}
	s.Log.Printf("power: %s requested from the shell", body.Action)
	dry, err := s.System.Power(r.Context(), body.Action)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 200, map[string]any{"ok": true, "dryRun": dry})
}

func (s *server) openURL(w http.ResponseWriter, r *http.Request) {
	var body struct {
		URL string `json:"url"`
	}
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	s.done(w, r, s.System.OpenURL(r.Context(), body.URL))
}

type toggle struct {
	Enabled *bool `json:"enabled"`
}

func (s *server) readToggle(w http.ResponseWriter, r *http.Request) (bool, bool) {
	var body toggle
	if err := readJSON(r, &body); err != nil || body.Enabled == nil {
		s.fail(w, r, &store.ValidationError{Msg: `expected {"enabled": true|false}`})
		return false, false
	}
	return *body.Enabled, true
}

func (s *server) setWifi(w http.ResponseWriter, r *http.Request) {
	if on, ok := s.readToggle(w, r); ok {
		s.done(w, r, s.System.SetWifi(r.Context(), on))
	}
}

func (s *server) wifiNetworks(w http.ResponseWriter, r *http.Request) {
	nets, err := s.System.WifiNetworks(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if nets == nil {
		nets = []system.WifiNetwork{}
	}
	writeJSON(w, 200, map[string]any{"networks": nets})
}

func (s *server) wifiConnect(w http.ResponseWriter, r *http.Request) {
	var body struct{ SSID, Password string }
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	s.done(w, r, s.System.WifiConnect(r.Context(), body.SSID, body.Password))
}

func (s *server) setBluetooth(w http.ResponseWriter, r *http.Request) {
	if on, ok := s.readToggle(w, r); ok {
		s.done(w, r, s.System.SetBluetooth(r.Context(), on))
	}
}

func (s *server) bluetoothDevices(w http.ResponseWriter, r *http.Request) {
	devs, err := s.System.BluetoothDevices(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if devs == nil {
		devs = []system.BTDevice{}
	}
	writeJSON(w, 200, map[string]any{"devices": devs})
}

func (s *server) bluetoothAction(w http.ResponseWriter, r *http.Request) {
	action := r.PathValue("action")
	switch action {
	case "pair", "connect", "disconnect", "remove":
	default:
		http.NotFound(w, r)
		return
	}
	var body struct{ Address string }
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	s.done(w, r, s.System.BluetoothAction(r.Context(), action, body.Address))
}

func (s *server) audio(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Volume *int  `json:"volume"`
		Muted  *bool `json:"muted"`
	}
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	if body.Volume == nil && body.Muted == nil {
		s.fail(w, r, &store.ValidationError{Msg: "nothing to change"})
		return
	}
	s.done(w, r, s.System.SetAudio(r.Context(), body.Volume, body.Muted))
}

func (s *server) backlight(w http.ResponseWriter, r *http.Request) {
	var body struct {
		Percent *int `json:"percent"`
	}
	if err := readJSON(r, &body); err != nil || body.Percent == nil {
		s.fail(w, r, &store.ValidationError{Msg: `expected {"percent": 1..100}`})
		return
	}
	s.done(w, r, s.System.SetBacklight(r.Context(), *body.Percent))
}

func (s *server) timezones(w http.ResponseWriter, r *http.Request) {
	zones, current, err := s.System.Timezones(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 200, map[string]any{"timezones": zones, "current": current})
}

func (s *server) setTimezone(w http.ResponseWriter, r *http.Request) {
	var body struct{ Timezone string }
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	s.done(w, r, s.System.SetTimezone(r.Context(), body.Timezone))
}

// ---------- weather ----------

func (s *server) weather(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, 200, s.Weather.Current(r.Context(), s.Settings.WeatherPlace()))
}

func (s *server) weatherPlace(w http.ResponseWriter, r *http.Request) {
	var body struct{ Query string }
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	place, err := s.Weather.Geocode(r.Context(), body.Query)
	if err != nil {
		s.fail(w, r, &store.ValidationError{Msg: err.Error()})
		return
	}
	if err := s.Settings.SetWeatherPlace(&place); err != nil {
		s.fail(w, r, err)
		return
	}
	label := place.Name
	if place.Country != "" {
		label += ", " + place.Country
	}
	writeJSON(w, 200, map[string]any{"ok": true, "place": label})
}

// ---------- storage ----------

func (s *server) storageCandidates(w http.ResponseWriter, r *http.Request) {
	disks, err := s.System.StorageCandidates(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if disks == nil {
		disks = []system.Disk{}
	}
	writeJSON(w, 200, map[string]any{"disks": disks})
}

func (s *server) storageSetup(w http.ResponseWriter, r *http.Request) {
	var body struct{ Device, Confirm string }
	if err := readJSON(r, &body); err != nil {
		s.fail(w, r, err)
		return
	}
	if body.Device == "" || body.Confirm != body.Device {
		s.fail(w, r, &store.ValidationError{Msg: "type the disk name exactly to confirm"})
		return
	}
	disks, err := s.System.StorageCandidates(r.Context())
	if err != nil {
		s.fail(w, r, err)
		return
	}
	blank := false
	for _, d := range disks {
		blank = blank || d.Name == body.Device
	}
	if !blank {
		s.fail(w, r, &store.ValidationError{Msg: "that disk is not empty or no longer attached; nothing was changed"})
		return
	}
	s.Log.Printf("storage: setting up %s as NOVA-DATA (confirmed by the user)", body.Device)
	if err := s.System.SetupStorage(r.Context(), body.Device); err != nil {
		s.fail(w, r, err)
		return
	}
	if s.ReloadStores != nil {
		s.ReloadStores()
	}
	writeJSON(w, 200, map[string]bool{"ok": true})
}

// ---------- USB drives ----------

func (s *server) readID(w http.ResponseWriter, r *http.Request) (string, bool) {
	var body struct{ ID string }
	if err := readJSON(r, &body); err != nil || !system.ValidBlockName(body.ID) {
		s.fail(w, r, &store.ValidationError{Msg: "invalid drive"})
		return "", false
	}
	return body.ID, true
}

func (s *server) usbMount(w http.ResponseWriter, r *http.Request) {
	if id, ok := s.readID(w, r); ok {
		s.done(w, r, s.System.USBMount(r.Context(), id))
	}
}

func (s *server) usbUnmount(w http.ResponseWriter, r *http.Request) {
	if id, ok := s.readID(w, r); ok {
		s.done(w, r, s.System.USBUnmount(r.Context(), id))
	}
}

var textExt = map[string]bool{".txt": true, ".md": true, ".csv": true, ".json": true, ".log": true}

const maxImportBytes = store.MaxContentBytes

// usbTextFiles lists small text files up to two levels deep. WalkDir never follows symlinks.
func usbTextFiles(root string) []map[string]any {
	var out []map[string]any
	_ = filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		if d.IsDir() {
			if rel != "." && (strings.HasPrefix(d.Name(), ".") || strings.Count(rel, string(filepath.Separator)) >= 1) {
				return filepath.SkipDir
			}
			return nil
		}
		if !d.Type().IsRegular() || strings.HasPrefix(d.Name(), ".") || !textExt[strings.ToLower(filepath.Ext(p))] {
			return nil
		}
		if info, err := d.Info(); err == nil && info.Size() <= maxImportBytes && len(out) < 200 {
			out = append(out, map[string]any{"path": filepath.ToSlash(rel), "sizeBytes": info.Size()})
		}
		return nil
	})
	return out
}

func (s *server) usbFiles(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if !system.ValidBlockName(id) {
		s.fail(w, r, &store.ValidationError{Msg: "invalid drive"})
		return
	}
	root, err := s.System.USBMountpoint(r.Context(), id)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	files := usbTextFiles(root)
	if files == nil {
		files = []map[string]any{}
	}
	writeJSON(w, 200, map[string]any{"files": files})
}

func (s *server) usbImport(w http.ResponseWriter, r *http.Request) {
	var body struct{ ID, Path string }
	if err := readJSON(r, &body); err != nil || !system.ValidBlockName(body.ID) {
		s.fail(w, r, &store.ValidationError{Msg: "invalid request"})
		return
	}
	root, err := s.System.USBMountpoint(r.Context(), body.ID)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	// Only a path that the listing itself produced is accepted.
	listed := false
	for _, f := range usbTextFiles(root) {
		listed = listed || f["path"] == body.Path
	}
	if !listed {
		s.fail(w, r, &store.ValidationError{Msg: "that file is not available on the drive"})
		return
	}
	full := filepath.Join(root, filepath.FromSlash(body.Path))
	info, err := os.Lstat(full)
	if err != nil || !info.Mode().IsRegular() || info.Size() > maxImportBytes {
		s.fail(w, r, &store.ValidationError{Msg: "that file cannot be imported"})
		return
	}
	data, err := os.ReadFile(full)
	if err != nil || !utf8.Valid(data) {
		s.fail(w, r, &store.ValidationError{Msg: "only plain-text files can be imported"})
		return
	}
	rec, err := s.Records.Create(store.Record{AppType: "file", Title: filepath.Base(full), Content: string(data)})
	if err != nil {
		s.fail(w, r, err)
		return
	}
	writeJSON(w, 201, map[string]any{"record": rec})
}

// ---------- photos from ~/Pictures ----------

var imageExt = map[string]string{".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png", ".webp": "image/webp", ".gif": "image/gif", ".avif": "image/avif"}

func (s *server) photos(w http.ResponseWriter, r *http.Request) {
	type photo struct {
		Name string `json:"name"`
		URL  string `json:"url"`
		mod  time.Time
	}
	out := []photo{}
	entries, _ := os.ReadDir(s.PicturesDir)
	for _, e := range entries {
		name := e.Name()
		if store.ValidName(name) != nil || !e.Type().IsRegular() || imageExt[strings.ToLower(filepath.Ext(name))] == "" {
			continue
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		out = append(out, photo{Name: name, URL: "/api/photos/file?name=" + url.QueryEscape(name), mod: info.ModTime()})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].mod.After(out[j].mod) })
	if len(out) > 200 {
		out = out[:200]
	}
	writeJSON(w, 200, map[string]any{"photos": out})
}

func (s *server) photoFile(w http.ResponseWriter, r *http.Request) {
	name := r.URL.Query().Get("name")
	p, err := cleanJoin(s.PicturesDir, name)
	ctype := imageExt[strings.ToLower(filepath.Ext(name))]
	if err != nil || ctype == "" {
		http.NotFound(w, r)
		return
	}
	info, err := os.Lstat(p)
	if err != nil || !info.Mode().IsRegular() {
		http.NotFound(w, r)
		return
	}
	f, err := os.Open(p)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", ctype)
	w.Header().Set("Cache-Control", "private, max-age=60")
	http.ServeContent(w, r, "", info.ModTime(), f)
}
