package store

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

// Record is one item of the dataSdk contract the shell UI was written against.
type Record struct {
	ID        string `json:"__backendId"`
	AppType   string `json:"app_type"`
	Title     string `json:"title"`
	Content   string `json:"content"`
	Completed bool   `json:"completed"`
	CreatedAt string `json:"created_at"`
}

// Limits. The hosted SDK capped the workspace at 999 items; this store allows far more.
const (
	MaxRecords      = 50000
	MaxTitleRunes   = 200
	MaxContentBytes = 100 * 1024
)

var (
	ErrNotFound = errors.New("that item no longer exists")
	ErrFull     = fmt.Errorf("storage limit reached (%d items); delete something first", MaxRecords)
)

// ValidationError is returned for bad input; its message is safe to show to the user.
type ValidationError struct{ Msg string }

func (e *ValidationError) Error() string { return e.Msg }

func invalid(format string, a ...any) error { return &ValidationError{Msg: fmt.Sprintf(format, a...)} }

// Records stores notes and tasks in records.json. Files (app_type "file") go to a Documents directory
// when one is configured, so they are real files the user can find on disk.
type Records struct {
	mu      sync.Mutex
	path    string
	docs    *Documents
	recs    []Record
	loadErr error
}

type recordsFile struct {
	Version int      `json:"version"`
	Records []Record `json:"records"`
}

// OpenRecords loads dataDir/records.json. A damaged file makes the store read-only (see LoadError).
func OpenRecords(dataDir string, docs *Documents) *Records {
	r := &Records{path: filepath.Join(dataDir, "records.json"), docs: docs}
	r.Reload()
	return r
}

// Reload re-reads the file, e.g. after a new data disk was mounted over the data directory.
func (r *Records) Reload() {
	r.mu.Lock()
	defer r.mu.Unlock()
	var f recordsFile
	_, err := readJSON(r.path, &f)
	r.loadErr = err
	if err == nil {
		r.recs = f.Records
	} else {
		r.recs = nil
	}
}

// LoadError reports why the store is read-only, or nil.
func (r *Records) LoadError() error {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.loadErr
}

func newID() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		panic(err) // crypto/rand never fails on Linux
	}
	return hex.EncodeToString(b)
}

func validate(in Record) (Record, error) {
	switch in.AppType {
	case "note", "task", "file":
	default:
		return in, invalid("unknown item type %q", in.AppType)
	}
	in.Title = strings.TrimSpace(in.Title)
	if in.Title == "" {
		return in, invalid("a title is required")
	}
	if !utf8.ValidString(in.Title) || !utf8.ValidString(in.Content) {
		return in, invalid("text must be valid UTF-8")
	}
	if utf8.RuneCountInString(in.Title) > MaxTitleRunes {
		return in, invalid("the title is longer than %d characters", MaxTitleRunes)
	}
	if len(in.Content) > MaxContentBytes {
		return in, invalid("the text is larger than %d KB", MaxContentBytes/1024)
	}
	if _, err := time.Parse(time.RFC3339Nano, in.CreatedAt); err != nil {
		in.CreatedAt = time.Now().UTC().Format(time.RFC3339Nano)
	}
	if in.AppType != "task" {
		in.Completed = false
	}
	return in, nil
}

func (r *Records) save(next []Record) error {
	if err := writeJSONAtomic(r.path, recordsFile{Version: 1, Records: next}); err != nil {
		return fmt.Errorf("could not save: %w", err)
	}
	r.recs = next
	return nil
}

// List returns every record, newest first.
func (r *Records) List() ([]Record, error) {
	r.mu.Lock()
	out := append([]Record(nil), r.recs...)
	loadErr := r.loadErr
	r.mu.Unlock()
	if loadErr != nil {
		return nil, loadErr
	}
	if r.docs != nil {
		files, err := r.docs.List()
		if err != nil {
			return nil, err
		}
		out = append(out, files...)
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].CreatedAt > out[j].CreatedAt })
	return out, nil
}

// Create validates and stores a new record and returns it with its id.
func (r *Records) Create(in Record) (Record, error) {
	rec, err := validate(in)
	if err != nil {
		return Record{}, err
	}
	if rec.AppType == "file" && r.docs != nil {
		return r.docs.Create(rec)
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.loadErr != nil {
		return Record{}, r.loadErr
	}
	if len(r.recs) >= MaxRecords {
		return Record{}, ErrFull
	}
	rec.ID = newID()
	next := append(append([]Record(nil), r.recs...), rec)
	if err := r.save(next); err != nil {
		return Record{}, err
	}
	return rec, nil
}

// Update replaces the record with the given id. created_at and app_type never change.
func (r *Records) Update(id string, in Record) (Record, error) {
	if IsDocumentID(id) {
		if r.docs == nil {
			return Record{}, ErrNotFound
		}
		in.AppType = "file"
		rec, err := validate(in)
		if err != nil {
			return Record{}, err
		}
		return r.docs.Update(id, rec)
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.loadErr != nil {
		return Record{}, r.loadErr
	}
	for i, old := range r.recs {
		if old.ID != id {
			continue
		}
		in.AppType = old.AppType
		rec, err := validate(in)
		if err != nil {
			return Record{}, err
		}
		rec.ID, rec.CreatedAt = old.ID, old.CreatedAt
		next := append([]Record(nil), r.recs...)
		next[i] = rec
		if err := r.save(next); err != nil {
			return Record{}, err
		}
		return rec, nil
	}
	return Record{}, ErrNotFound
}

// Delete removes the record with the given id.
func (r *Records) Delete(id string) error {
	if IsDocumentID(id) {
		if r.docs == nil {
			return ErrNotFound
		}
		return r.docs.Delete(id)
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.loadErr != nil {
		return r.loadErr
	}
	for i, old := range r.recs {
		if old.ID == id {
			next := append(append([]Record(nil), r.recs[:i]...), r.recs[i+1:]...)
			return r.save(next)
		}
	}
	return ErrNotFound
}
