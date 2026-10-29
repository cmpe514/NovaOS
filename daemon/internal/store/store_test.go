package store

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestRecordsCRUDAndOrder(t *testing.T) {
	dir := t.TempDir()
	r := OpenRecords(dir, nil)
	first, err := r.Create(Record{AppType: "note", Title: " First ", Content: "a", CreatedAt: "2026-01-01T10:00:00.000Z"})
	if err != nil {
		t.Fatal(err)
	}
	if first.Title != "First" || first.ID == "" {
		t.Fatalf("bad record %+v", first)
	}
	second, err := r.Create(Record{AppType: "task", Title: "Second", CreatedAt: "2026-01-02T10:00:00.000Z"})
	if err != nil {
		t.Fatal(err)
	}
	list, _ := r.List()
	if len(list) != 2 || list[0].ID != second.ID {
		t.Fatalf("want newest first, got %+v", list)
	}
	upd, err := r.Update(second.ID, Record{AppType: "note", Title: "Second", Completed: true, CreatedAt: "1999-01-01T00:00:00Z"})
	if err != nil {
		t.Fatal(err)
	}
	if !upd.Completed || upd.AppType != "task" || upd.CreatedAt != second.CreatedAt {
		t.Fatalf("update must keep type and created_at: %+v", upd)
	}
	// Persisted: a fresh store sees the same data.
	again := OpenRecords(dir, nil)
	list, _ = again.List()
	if len(list) != 2 || !list[0].Completed {
		t.Fatalf("not persisted: %+v", list)
	}
	if err := again.Delete(first.ID); err != nil {
		t.Fatal(err)
	}
	if err := again.Delete(first.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
	if _, err := again.Update("nope", Record{AppType: "task", Title: "x"}); !errors.Is(err, ErrNotFound) {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestRecordsValidation(t *testing.T) {
	r := OpenRecords(t.TempDir(), nil)
	cases := []Record{
		{AppType: "bogus", Title: "x"},
		{AppType: "note", Title: "   "},
		{AppType: "note", Title: strings.Repeat("x", MaxTitleRunes+1)},
		{AppType: "note", Title: "x", Content: strings.Repeat("x", MaxContentBytes+1)},
		{AppType: "note", Title: "bad \xff utf8"},
	}
	for _, c := range cases {
		var verr *ValidationError
		if _, err := r.Create(c); !errors.As(err, &verr) {
			t.Errorf("%+v: want ValidationError, got %v", c, err)
		}
	}
	rec, err := r.Create(Record{AppType: "note", Title: "ok", Completed: true, CreatedAt: "garbage"})
	if err != nil {
		t.Fatal(err)
	}
	if rec.Completed {
		t.Error("only tasks can be completed")
	}
	if _, err := time.Parse(time.RFC3339Nano, rec.CreatedAt); err != nil {
		t.Errorf("created_at not normalised: %q", rec.CreatedAt)
	}
}

func TestDamagedFileIsNeverOverwritten(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "records.json")
	if err := os.WriteFile(path, []byte("{not json"), 0o600); err != nil {
		t.Fatal(err)
	}
	r := OpenRecords(dir, nil)
	if r.LoadError() == nil {
		t.Fatal("expected a load error")
	}
	if _, err := r.List(); err == nil {
		t.Fatal("List must report the damage")
	}
	if _, err := r.Create(Record{AppType: "note", Title: "x"}); err == nil {
		t.Fatal("Create must refuse to overwrite a damaged file")
	}
	data, _ := os.ReadFile(path)
	if string(data) != "{not json" {
		t.Fatal("damaged file was modified")
	}
}

func TestAtomicWriteLeavesNoTempFiles(t *testing.T) {
	dir := t.TempDir()
	r := OpenRecords(dir, nil)
	for i := 0; i < 20; i++ {
		if _, err := r.Create(Record{AppType: "note", Title: "n"}); err != nil {
			t.Fatal(err)
		}
	}
	entries, _ := os.ReadDir(dir)
	if len(entries) != 1 || entries[0].Name() != "records.json" {
		t.Fatalf("unexpected files: %v", entries)
	}
	info, _ := os.Stat(filepath.Join(dir, "records.json"))
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("records.json must be 0600, is %v", info.Mode().Perm())
	}
}

func TestDocuments(t *testing.T) {
	data, docsDir := t.TempDir(), filepath.Join(t.TempDir(), "Documents")
	docs, err := NewDocuments(docsDir)
	if err != nil {
		t.Fatal(err)
	}
	r := OpenRecords(data, docs)
	rec, err := r.Create(Record{AppType: "file", Title: "shopping list.txt", Content: "milk\neggs"})
	if err != nil {
		t.Fatal(err)
	}
	if rec.ID != "doc:shopping list.txt" {
		t.Fatalf("id %q", rec.ID)
	}
	got, _ := os.ReadFile(filepath.Join(docsDir, "shopping list.txt"))
	if string(got) != "milk\neggs" {
		t.Fatalf("file content %q", got)
	}
	if _, err := r.Create(Record{AppType: "file", Title: "shopping list.txt"}); err == nil {
		t.Fatal("must not overwrite an existing file")
	}
	// Things that must never be touched or listed.
	os.WriteFile(filepath.Join(docsDir, ".hidden"), []byte("x"), 0o600)
	os.Mkdir(filepath.Join(docsDir, "folder"), 0o700)
	outside := filepath.Join(t.TempDir(), "secret")
	os.WriteFile(outside, []byte("secret"), 0o600)
	os.Symlink(outside, filepath.Join(docsDir, "link"))
	os.WriteFile(filepath.Join(docsDir, "binary.bin"), []byte{0xff, 0xfe, 0}, 0o600)
	list, err := r.List()
	if err != nil {
		t.Fatal(err)
	}
	names := map[string]string{}
	for _, l := range list {
		names[l.Title] = l.Content
	}
	if len(names) != 2 || names["binary.bin"] != "(This file is not plain text.)" {
		t.Fatalf("listing %v", names)
	}
	for _, id := range []string{"doc:link", "doc:../secret", "doc:folder", "doc:.hidden", "doc:a/b"} {
		if err := r.Delete(id); err == nil {
			t.Errorf("delete %s must fail", id)
		}
	}
	if _, err := os.Stat(outside); err != nil {
		t.Fatal("symlink target was touched")
	}
	if _, err := r.Update("doc:shopping list.txt", Record{Title: "shopping list.txt", Content: "bread"}); err != nil {
		t.Fatal(err)
	}
	got, _ = os.ReadFile(filepath.Join(docsDir, "shopping list.txt"))
	if string(got) != "bread" {
		t.Fatalf("update content %q", got)
	}
	if err := r.Delete("doc:shopping list.txt"); err != nil {
		t.Fatal(err)
	}
}

func TestValidName(t *testing.T) {
	for _, bad := range []string{"", " ", ".", "..", ".env", "a/b", `a\b`, "a\x00b", "a\nb", "trailing ", strings.Repeat("x", 121)} {
		if ValidName(bad) == nil {
			t.Errorf("%q should be rejected", bad)
		}
	}
	for _, good := range []string{"notes.txt", "Shopping list", "Ünïcødé ✓.md", "a..b"} {
		if err := ValidName(good); err != nil {
			t.Errorf("%q rejected: %v", good, err)
		}
	}
}

func TestSettings(t *testing.T) {
	dir := t.TempDir()
	s := OpenSettings(dir)
	out, err := s.SetUI(map[string]any{"alt": true, "accent": "peach", "scale": 200.0, "brightness": 10.0, "evil": "x", "focus": "yes"})
	if err != nil {
		t.Fatal(err)
	}
	if out["alt"] != true || out["accent"] != "peach" || out["scale"] != 120.0 || out["brightness"] != 45.0 {
		t.Fatalf("sanitised: %v", out)
	}
	if _, ok := out["evil"]; ok {
		t.Fatal("unknown key kept")
	}
	if _, ok := out["focus"]; ok {
		t.Fatal("wrong type kept")
	}
	if err := s.SetWeatherPlace(&Place{Name: "Lisbon", Latitude: 38.7, Longitude: -9.1}); err != nil {
		t.Fatal(err)
	}
	again := OpenSettings(dir)
	if again.UI()["accent"] != "peach" || again.WeatherPlace().Name != "Lisbon" {
		t.Fatal("settings not persisted")
	}
}
