package store

import (
	"math"
	"path/filepath"
	"sync"
)

// Place is the forecast location the user picked (weather is off until they do).
type Place struct {
	Name      string  `json:"name"`
	Country   string  `json:"country,omitempty"`
	Latitude  float64 `json:"latitude"`
	Longitude float64 `json:"longitude"`
	Timezone  string  `json:"timezone,omitempty"`
}

type settingsFile struct {
	Version int            `json:"version"`
	UI      map[string]any `json:"ui"`
	Weather *Place         `json:"weather,omitempty"`
}

// Settings persists the shell's UI settings (wallpaper mood, accent, text scale, …) and the weather place.
// Device state such as Wi-Fi or volume is not stored here: it is read from the device itself.
type Settings struct {
	mu      sync.Mutex
	path    string
	data    settingsFile
	loadErr error
}

// OpenSettings loads dataDir/settings.json.
func OpenSettings(dataDir string) *Settings {
	s := &Settings{path: filepath.Join(dataDir, "settings.json")}
	s.Reload()
	return s
}

// Reload re-reads the file.
func (s *Settings) Reload() {
	s.mu.Lock()
	defer s.mu.Unlock()
	var f settingsFile
	_, err := readJSON(s.path, &f)
	s.loadErr = err
	if err != nil {
		f = settingsFile{}
	}
	f.UI = SanitizeUI(f.UI)
	s.data = f
}

// LoadError reports why settings are read-only, or nil.
func (s *Settings) LoadError() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.loadErr
}

// SanitizeUI keeps only known keys with values of the right type and range (same rules as the UI).
func SanitizeUI(in map[string]any) map[string]any {
	out := map[string]any{}
	for _, key := range []string{"focus", "quiet", "alt", "motion", "clock24"} {
		if v, ok := in[key].(bool); ok {
			out[key] = v
		}
	}
	if v, ok := in["accent"].(string); ok && (v == "mint" || v == "peach" || v == "blue") {
		out["accent"] = v
	}
	clamp := func(key string, lo, hi float64) {
		if v, ok := in[key].(float64); ok && !math.IsNaN(v) && !math.IsInf(v, 0) {
			out[key] = math.Round(math.Min(hi, math.Max(lo, v)))
		}
	}
	clamp("brightness", 45, 110)
	clamp("scale", 90, 120)
	return out
}

// UI returns a copy of the UI settings.
func (s *Settings) UI() map[string]any {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := map[string]any{}
	for k, v := range s.data.UI {
		out[k] = v
	}
	return out
}

// SetUI merges the given keys (after sanitising) and saves.
func (s *Settings) SetUI(in map[string]any) (map[string]any, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.loadErr != nil {
		return nil, s.loadErr
	}
	next := s.data
	next.Version = 1
	next.UI = map[string]any{}
	for k, v := range s.data.UI {
		next.UI[k] = v
	}
	for k, v := range SanitizeUI(in) {
		next.UI[k] = v
	}
	if err := writeJSONAtomic(s.path, next); err != nil {
		return nil, err
	}
	s.data = next
	out := map[string]any{}
	for k, v := range next.UI {
		out[k] = v
	}
	return out, nil
}

// WeatherPlace returns the saved forecast location, or nil.
func (s *Settings) WeatherPlace() *Place {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.data.Weather == nil {
		return nil
	}
	p := *s.data.Weather
	return &p
}

// SetWeatherPlace saves the forecast location (nil clears it).
func (s *Settings) SetWeatherPlace(p *Place) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.loadErr != nil {
		return s.loadErr
	}
	next := s.data
	next.Version = 1
	next.Weather = p
	if err := writeJSONAtomic(s.path, next); err != nil {
		return err
	}
	s.data = next
	return nil
}
