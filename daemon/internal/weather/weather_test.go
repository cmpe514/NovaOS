package weather

import (
	"context"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"

	"github.com/cmpe514/novaos/daemon/internal/store"
)

func TestCurrentAndGeocode(t *testing.T) {
	var calls atomic.Int32
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		switch r.URL.Path {
		case "/forecast":
			if r.URL.Query().Get("current") != "temperature_2m,weather_code" {
				t.Errorf("query %v", r.URL.Query())
			}
			w.Write([]byte(`{"current":{"time":"2026-03-14T09:45","temperature_2m":21.4,"weather_code":2}}`))
		case "/search":
			if r.URL.Query().Get("name") == "Nowhere" {
				w.Write([]byte(`{}`))
				return
			}
			w.Write([]byte(`{"results":[{"name":"Lisbon","country":"Portugal","latitude":38.72,"longitude":-9.13,"timezone":"Europe/Lisbon"}]}`))
		}
	}))
	defer srv.Close()
	c := New("test")
	c.ForecastURL, c.GeocodingURL = srv.URL+"/forecast", srv.URL+"/search"

	if r := c.Current(context.Background(), nil); r.Live || r.Reason == "" {
		t.Fatalf("no place must give a sample with a reason: %+v", r)
	}
	if calls.Load() != 0 {
		t.Fatal("no request may be made before a place is chosen")
	}
	p, err := c.Geocode(context.Background(), "Lisbon")
	if err != nil || p.Name != "Lisbon" || p.Timezone != "Europe/Lisbon" {
		t.Fatalf("geocode %+v %v", p, err)
	}
	if _, err := c.Geocode(context.Background(), "Nowhere"); err == nil {
		t.Fatal("unknown place must fail")
	}
	r := c.Current(context.Background(), &p)
	if !r.Live || r.Temperature != 21.4 || r.Condition != "Partly cloudy" || r.Place != "Lisbon, Portugal" {
		t.Fatalf("report %+v", r)
	}
	before := calls.Load()
	c.Current(context.Background(), &p)
	if calls.Load() != before {
		t.Fatal("second call within 15 minutes must hit the cache")
	}
	srv.Close()
	other := store.Place{Name: "Porto", Latitude: 41.1, Longitude: -8.6}
	if r := c.Current(context.Background(), &other); r.Live || r.Reason == "" {
		t.Fatalf("offline must give a labelled sample: %+v", r)
	}
}
