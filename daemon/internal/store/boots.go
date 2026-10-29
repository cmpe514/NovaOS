package store

import (
	"path/filepath"
	"time"
)

// CountBoot records that the shell came up once more and returns the new count. It is the canary for
// persistence: tools/smoke-qemu.sh boots twice with one data disk and expects boots=2 the second time,
// which proves the disk was mounted, written and cleanly unmounted at shutdown.
func CountBoot(dataDir string) (int, error) {
	path := filepath.Join(dataDir, "boots.json")
	var b struct {
		Boots int    `json:"boots"`
		Last  string `json:"last"`
	}
	if _, err := readJSON(path, &b); err != nil {
		return 0, err
	}
	b.Boots++
	b.Last = time.Now().UTC().Format(time.RFC3339)
	return b.Boots, writeJSONAtomic(path, b)
}
