package store

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

// Documents maps Files-app records onto regular files in one directory (the user's ~/Documents).
// Only plain names directly inside that directory are ever touched: no separators, no "..", no hidden
// files, no symlinks. The record id is "doc:" + the file name.
type Documents struct{ dir string }

const (
	docPrefix       = "doc:"
	maxNameBytes    = 120
	maxPreviewBytes = 256 * 1024
)

// NewDocuments uses dir, creating it (0700) if needed.
func NewDocuments(dir string) (*Documents, error) {
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return nil, err
	}
	return &Documents{dir: dir}, nil
}

// Dir is the directory the documents live in.
func (d *Documents) Dir() string { return d.dir }

// IsDocumentID reports whether id names a file in Documents rather than a stored record.
func IsDocumentID(id string) bool { return strings.HasPrefix(id, docPrefix) }

// ValidName checks a user-supplied file name. It is also used for USB imports.
func ValidName(name string) error {
	switch {
	case name == "" || strings.TrimSpace(name) == "":
		return invalid("a file name is required")
	case len(name) > maxNameBytes:
		return invalid("the file name is longer than %d bytes", maxNameBytes)
	case !utf8.ValidString(name):
		return invalid("the file name must be valid UTF-8")
	case name == "." || name == ".." || strings.HasPrefix(name, "."):
		return invalid("file names cannot start with a dot")
	case strings.ContainsAny(name, `/\`):
		return invalid("file names cannot contain / or \\")
	case strings.HasSuffix(name, " "):
		return invalid("file names cannot end with a space")
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return invalid("file names cannot contain control characters")
		}
	}
	return nil
}

// path resolves a validated name to a path that is guaranteed to sit directly inside d.dir.
func (d *Documents) path(name string) (string, error) {
	if err := ValidName(name); err != nil {
		return "", err
	}
	p := filepath.Join(d.dir, name)
	if filepath.Dir(p) != filepath.Clean(d.dir) {
		return "", invalid("invalid file name")
	}
	return p, nil
}

func (d *Documents) nameFromID(id string) (string, error) {
	if !IsDocumentID(id) {
		return "", ErrNotFound
	}
	return strings.TrimPrefix(id, docPrefix), nil
}

func (d *Documents) regular(p string) (fs.FileInfo, error) {
	info, err := os.Lstat(p)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() {
		return nil, invalid("%s is not a regular file", info.Name())
	}
	return info, nil
}

func (d *Documents) record(name string, info fs.FileInfo) Record {
	rec := Record{ID: docPrefix + name, AppType: "file", Title: name, CreatedAt: info.ModTime().UTC().Format(time.RFC3339Nano)}
	if info.Size() > maxPreviewBytes {
		rec.Content = fmt.Sprintf("(Large file, %d KB — too big to show here.)", info.Size()/1024)
		return rec
	}
	data, err := os.ReadFile(filepath.Join(d.dir, name))
	switch {
	case err != nil:
		rec.Content = "(This file could not be read.)"
	case !utf8.Valid(data):
		rec.Content = "(This file is not plain text.)"
	default:
		rec.Content = string(data)
	}
	return rec
}

// List returns one record per regular, non-hidden file.
func (d *Documents) List() ([]Record, error) {
	entries, err := os.ReadDir(d.dir)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, fmt.Errorf("could not read Documents: %w", err)
	}
	var out []Record
	for _, e := range entries {
		name := e.Name()
		if ValidName(name) != nil || !e.Type().IsRegular() {
			continue // hidden files, symlinks, directories, odd names
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		out = append(out, d.record(name, info))
	}
	return out, nil
}

// Create writes a new file; it never overwrites an existing one.
func (d *Documents) Create(rec Record) (Record, error) {
	p, err := d.path(rec.Title)
	if err != nil {
		return Record{}, err
	}
	if err := writeNewFileAtomic(p, []byte(rec.Content)); err != nil {
		return Record{}, err
	}
	info, err := d.regular(p)
	if err != nil {
		return Record{}, err
	}
	return d.record(rec.Title, info), nil
}

// Update rewrites a file's contents. Renaming is not supported (the shell never renames).
func (d *Documents) Update(id string, rec Record) (Record, error) {
	name, err := d.nameFromID(id)
	if err != nil {
		return Record{}, err
	}
	if rec.Title != name {
		return Record{}, invalid("renaming files is not supported")
	}
	p, err := d.path(name)
	if err != nil {
		return Record{}, err
	}
	if _, err := d.regular(p); err != nil {
		return Record{}, err
	}
	if err := writeFileAtomic(p, []byte(rec.Content), 0o600); err != nil {
		return Record{}, fmt.Errorf("could not save %s: %w", name, err)
	}
	info, err := d.regular(p)
	if err != nil {
		return Record{}, err
	}
	return d.record(name, info), nil
}

// Delete removes a regular file.
func (d *Documents) Delete(id string) error {
	name, err := d.nameFromID(id)
	if err != nil {
		return err
	}
	p, err := d.path(name)
	if err != nil {
		return err
	}
	if _, err := d.regular(p); err != nil {
		return err
	}
	if err := os.Remove(p); err != nil {
		return fmt.Errorf("could not delete %s: %w", name, err)
	}
	return syncDir(d.dir)
}

// writeNewFileAtomic creates path with data, failing if it already exists. The content is written to a
// temporary file first and hard-linked into place, so the new name never shows a partial file.
func writeNewFileAtomic(path string, data []byte) error {
	dir := filepath.Dir(path)
	tmp, err := os.CreateTemp(dir, ".nova-new.*.tmp")
	if err != nil {
		return err
	}
	name := tmp.Name()
	defer os.Remove(name)
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if err := os.Link(name, path); err != nil {
		if errors.Is(err, fs.ErrExist) {
			return invalid("a file named %q already exists", filepath.Base(path))
		}
		return err
	}
	return syncDir(dir)
}
