package system

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
)

// Runner executes only allow-listed programs, by absolute path, with an argument vector (no shell),
// a fixed environment and a context deadline. Every call site validates its arguments first.
type Runner struct {
	paths map[string]string
	env   []string
}

// Tools novad may run. Anything else is impossible to execute through the runner.
var allowedTools = []string{"systemctl", "nmcli", "bluetoothctl", "rfkill", "wpctl", "busctl", "timedatectl", "lsblk", "findmnt", "udisksctl", "chromium", "swaymsg"}

var searchDirs = []string{"/usr/bin", "/usr/sbin", "/bin", "/sbin"}

// NewRunner resolves the allow-listed tools that are installed. passEnv names environment variables
// to hand on (session bus, Wayland display, runtime dir); everything else is dropped.
func NewRunner(passEnv ...string) *Runner { return NewRunnerIn(searchDirs, passEnv...) }

// NewRunnerIn is NewRunner with explicit search directories (tests use a directory of stub tools).
func NewRunnerIn(dirs []string, passEnv ...string) *Runner {
	r := &Runner{paths: map[string]string{}, env: []string{"PATH=/usr/bin:/usr/sbin:/bin:/sbin", "LANG=C.UTF-8", "LC_ALL=C.UTF-8"}}
	for _, tool := range allowedTools {
		for _, dir := range dirs {
			p := filepath.Join(dir, tool)
			if info, err := os.Stat(p); err == nil && info.Mode().IsRegular() && info.Mode()&0o111 != 0 {
				r.paths[tool] = p
				break
			}
		}
	}
	for _, key := range passEnv {
		if v, ok := os.LookupEnv(key); ok {
			r.env = append(r.env, key+"="+v)
		}
	}
	return r
}

// Has reports whether a tool is available.
func (r *Runner) Has(tool string) bool { _, ok := r.paths[tool]; return ok }

func (r *Runner) command(ctx context.Context, tool string, args []string) (*exec.Cmd, error) {
	path, ok := r.paths[tool]
	if !ok {
		return nil, fmt.Errorf("%s: %w", tool, ErrUnsupported)
	}
	cmd := exec.CommandContext(ctx, path, args...)
	cmd.Env = r.env
	cmd.Dir = "/"
	return cmd, nil
}

// Run runs a tool and returns its standard output. A failure includes the first line of stderr.
func (r *Runner) Run(ctx context.Context, tool string, args ...string) ([]byte, error) {
	cmd, err := r.command(ctx, tool, args)
	if err != nil {
		return nil, err
	}
	var stdout, stderr bytes.Buffer
	cmd.Stdout, cmd.Stderr = &stdout, &stderr
	if err := cmd.Run(); err != nil {
		if ctx.Err() != nil {
			return stdout.Bytes(), ctx.Err()
		}
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = strings.TrimSpace(stdout.String())
		}
		if i := strings.IndexByte(msg, '\n'); i >= 0 {
			msg = msg[:i]
		}
		return stdout.Bytes(), fmt.Errorf("%s failed: %s", tool, firstNonEmpty(msg, err.Error()))
	}
	return stdout.Bytes(), nil
}

// Start launches a long-running program (the web browser) in its own session and reaps it later.
func (r *Runner) Start(tool string, args ...string) error {
	cmd, err := r.command(context.Background(), tool, args)
	if err != nil {
		return err
	}
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	cmd.Stdout, cmd.Stderr = os.Stderr, os.Stderr // ends up in the session journal
	if err := cmd.Start(); err != nil {
		return err
	}
	go cmd.Wait() //nolint:errcheck // the browser's exit status is not interesting
	return nil
}

func firstNonEmpty(a, b string) string {
	if a != "" {
		return a
	}
	return b
}
