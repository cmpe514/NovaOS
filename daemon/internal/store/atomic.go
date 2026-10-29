// Package store keeps NovaOS user data: notes, tasks and files (the dataSdk records) and UI settings.
// Every write is atomic: the new content goes to a temporary file in the same directory, is fsynced,
// renamed over the old file and the directory is fsynced, so a power cut leaves either the old or the
// new version, never a torn file. A file that cannot be parsed is never overwritten: the store goes
// read-only and reports the problem instead of silently starting from scratch.
package store

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// writeFileAtomic replaces path with data.
func writeFileAtomic(path string, data []byte, perm os.FileMode) error {
	dir := filepath.Dir(path)
	tmp, err := os.CreateTemp(dir, "."+filepath.Base(path)+".*.tmp")
	if err != nil {
		return err
	}
	name := tmp.Name()
	cleanup := func() { _ = os.Remove(name) }
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		cleanup()
		return err
	}
	if err := tmp.Chmod(perm); err != nil {
		tmp.Close()
		cleanup()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		cleanup()
		return err
	}
	if err := tmp.Close(); err != nil {
		cleanup()
		return err
	}
	if err := os.Rename(name, path); err != nil {
		cleanup()
		return err
	}
	return syncDir(dir)
}

func syncDir(dir string) error {
	d, err := os.Open(dir)
	if err != nil {
		return err
	}
	defer d.Close()
	return d.Sync()
}

// writeJSONAtomic marshals v (indented, for humans doing recovery) and writes it atomically.
func writeJSONAtomic(path string, v any) error {
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(path, append(data, '\n'), 0o600)
}

// readJSON loads path into v. A missing file is not an error (found=false).
func readJSON(path string, v any) (found bool, err error) {
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	if err := json.Unmarshal(data, v); err != nil {
		return true, fmt.Errorf("%s is damaged (%v); it was left untouched", filepath.Base(path), err)
	}
	return true, nil
}
