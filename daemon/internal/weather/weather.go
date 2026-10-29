// Package weather fetches current conditions from Open-Meteo (https://open-meteo.com), a free, keyless
// API (non-commercial use, attribution "Weather data by Open-Meteo.com", CC BY 4.0). Nothing is fetched
// until the user picks a forecast location, so a fresh device makes no weather requests at all.
// Results are cached for 15 minutes, far below Open-Meteo's fair-use limits.
package weather

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"github.com/cmpe514/novaos/daemon/internal/store"
)

const (
	DefaultForecastURL  = "https://api.open-meteo.com/v1/forecast"
	DefaultGeocodingURL = "https://geocoding-api.open-meteo.com/v1/search"
	cacheFor            = 15 * time.Minute
)

// Report is what the Weather app shows. Live=false means "show the labelled sample".
type Report struct {
	Live        bool    `json:"live"`
	Place       string  `json:"place,omitempty"`
	PlaceName   string  `json:"placeName,omitempty"`
	Temperature float64 `json:"temperature,omitempty"`
	Condition   string  `json:"condition,omitempty"`
	Updated     string  `json:"updated,omitempty"`
	Reason      string  `json:"reason,omitempty"`
}

// Client talks to Open-Meteo.
type Client struct {
	ForecastURL, GeocodingURL string
	HTTP                      *http.Client
	UserAgent                 string

	mu      sync.Mutex
	cached  Report
	cacheAt time.Time
	cacheOf store.Place
}

// New returns a client with the public Open-Meteo endpoints.
func New(userAgent string) *Client {
	return &Client{ForecastURL: DefaultForecastURL, GeocodingURL: DefaultGeocodingURL, HTTP: &http.Client{Timeout: 8 * time.Second}, UserAgent: userAgent}
}

func (c *Client) get(ctx context.Context, base string, q url.Values, v any) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, base+"?"+q.Encode(), nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", c.UserAgent)
	res, err := c.HTTP.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode != http.StatusOK {
		return fmt.Errorf("Open-Meteo answered %s", res.Status)
	}
	return json.NewDecoder(http.MaxBytesReader(nil, res.Body, 1<<20)).Decode(v)
}

// Geocode resolves a city name to a place.
func (c *Client) Geocode(ctx context.Context, query string) (store.Place, error) {
	query = strings.TrimSpace(query)
	if query == "" || utf8.RuneCountInString(query) > 80 {
		return store.Place{}, errors.New("enter a city name (up to 80 characters)")
	}
	var out struct {
		Results []struct {
			Name      string  `json:"name"`
			Country   string  `json:"country"`
			Latitude  float64 `json:"latitude"`
			Longitude float64 `json:"longitude"`
			Timezone  string  `json:"timezone"`
		} `json:"results"`
	}
	q := url.Values{"name": {query}, "count": {"1"}, "language": {"en"}, "format": {"json"}}
	if err := c.get(ctx, c.GeocodingURL, q, &out); err != nil {
		return store.Place{}, fmt.Errorf("could not look up %q: %w", query, err)
	}
	if len(out.Results) == 0 {
		return store.Place{}, fmt.Errorf("no place called %q was found", query)
	}
	r := out.Results[0]
	return store.Place{Name: r.Name, Country: r.Country, Latitude: r.Latitude, Longitude: r.Longitude, Timezone: r.Timezone}, nil
}

// Current returns live conditions for place, or a sample report with the reason.
func (c *Client) Current(ctx context.Context, place *store.Place) Report {
	if place == nil {
		return Report{Reason: "Choose a forecast location in Settings to see live weather."}
	}
	label := place.Name
	if place.Country != "" {
		label += ", " + place.Country
	}
	c.mu.Lock()
	if c.cacheOf == *place && time.Since(c.cacheAt) < cacheFor {
		r := c.cached
		c.mu.Unlock()
		return r
	}
	c.mu.Unlock()
	var out struct {
		Current struct {
			Time        string  `json:"time"`
			Temperature float64 `json:"temperature_2m"`
			Code        int     `json:"weather_code"`
		} `json:"current"`
	}
	q := url.Values{
		"latitude":  {fmt.Sprintf("%.4f", place.Latitude)},
		"longitude": {fmt.Sprintf("%.4f", place.Longitude)},
		"current":   {"temperature_2m,weather_code"},
		"timezone":  {"auto"},
	}
	if err := c.get(ctx, c.ForecastURL, q, &out); err != nil {
		return Report{PlaceName: place.Name, Reason: "The live forecast for " + label + " is unavailable (offline?)."}
	}
	r := Report{Live: true, Place: label, PlaceName: place.Name, Temperature: out.Current.Temperature, Condition: Describe(out.Current.Code), Updated: time.Now().UTC().Format(time.RFC3339)}
	c.mu.Lock()
	c.cached, c.cacheAt, c.cacheOf = r, time.Now(), *place
	c.mu.Unlock()
	return r
}

// Describe turns a WMO weather interpretation code into words.
func Describe(code int) string {
	switch {
	case code == 0:
		return "Clear sky"
	case code == 1:
		return "Mainly clear"
	case code == 2:
		return "Partly cloudy"
	case code == 3:
		return "Overcast"
	case code == 45 || code == 48:
		return "Fog"
	case code >= 51 && code <= 57:
		return "Drizzle"
	case code >= 61 && code <= 67:
		return "Rain"
	case code >= 71 && code <= 77:
		return "Snow"
	case code >= 80 && code <= 82:
		return "Rain showers"
	case code == 85 || code == 86:
		return "Snow showers"
	case code >= 95:
		return "Thunderstorm"
	}
	return "Unknown conditions"
}
