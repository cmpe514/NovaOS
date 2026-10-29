// novad — the NovaOS daemon. It serves the shell UI, the data API (notes, tasks, files, settings) and
// the system API on 127.0.0.1 only, and runs as the unprivileged session user.
//
//	novad [serve] [flags]     run the daemon (default)
//	novad login-url [flags]   print the one-time sign-in URL for the shell browser
//	novad wait [flags]        wait until /healthz answers (used by the session launcher)
//	novad bar                 swaybar status command: Home button + clock
//	novad watch               shows the bar off the shell workspace; returns home from an empty browser workspace
//	novad version
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/cmpe514/novaos/daemon/internal/auth"
	"github.com/cmpe514/novaos/daemon/internal/server"
	"github.com/cmpe514/novaos/daemon/internal/store"
	"github.com/cmpe514/novaos/daemon/internal/system"
	"github.com/cmpe514/novaos/daemon/internal/weather"
)

// Set at build time: -ldflags "-X main.version=… -X main.brand=…"
var (
	version = "dev"
	brand   = "NovaOS"
)

func defaultRuntimeDir() string {
	if d := os.Getenv("XDG_RUNTIME_DIR"); d != "" {
		return filepath.Join(d, "novaos")
	}
	return filepath.Join(os.TempDir(), fmt.Sprintf("novaos-%d", os.Getuid()))
}

func osRelease() string {
	data, err := os.ReadFile("/etc/os-release")
	if err != nil {
		return ""
	}
	for _, line := range strings.Split(string(data), "\n") {
		if v, ok := strings.CutPrefix(line, "PRETTY_NAME="); ok {
			return strings.Trim(v, `"`)
		}
	}
	return ""
}

type common struct {
	listen, runtimeDir string
}

func (c *common) register(fs *flag.FlagSet) {
	fs.StringVar(&c.listen, "listen", "127.0.0.1:7171", "address to listen on (loopback only)")
	fs.StringVar(&c.runtimeDir, "runtime-dir", defaultRuntimeDir(), "directory for per-boot secrets (0700)")
}

func main() {
	args := os.Args[1:]
	cmd := "serve"
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		cmd, args = args[0], args[1:]
	}
	switch cmd {
	case "serve":
		os.Exit(serve(args))
	case "login-url":
		os.Exit(loginURL(args))
	case "wait":
		os.Exit(wait(args))
	case "bar":
		os.Exit(bar(args))
	case "watch":
		os.Exit(watch(args))
	case "version":
		fmt.Printf("novad %s (%s)\n", version, brand)
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q (serve, login-url, wait, bar, watch, version)\n", cmd)
		os.Exit(2)
	}
}

func serve(args []string) int {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	var c common
	c.register(fs)
	home, _ := os.UserHomeDir()
	uiDir := fs.String("ui", "/usr/share/novaos/ui", "directory with the built shell UI")
	dataDir := fs.String("data", "/var/lib/novaos/data", "directory for records.json and settings.json")
	dataMount := fs.String("data-mount", "/var/lib/novaos", "mountpoint of the NOVA-DATA partition")
	homeDir := fs.String("home", home, "home directory (Documents, Pictures, web browser profile)")
	sys := fs.String("system", "linux", "system backend: linux or fake")
	dev := fs.Bool("dev", false, "development mode: fake system backend, simulated power actions, prints the sign-in URL")
	name := fs.String("name", brand, "product name shown in the UI")
	fakeStorage := fs.String("fake-storage", "persistent", "fake backend only: persistent, or blank (no NOVA-DATA disk yet; offers setup)")
	fs.Parse(args)
	logger := log.New(os.Stderr, "novad: ", 0)

	if *dev && !flagSet(fs, "system") {
		*sys = "fake"
	}
	for _, d := range []string{*dataDir, *homeDir} {
		if err := os.MkdirAll(d, 0o700); err != nil {
			logger.Printf("cannot create %s: %v", d, err)
			return 1
		}
	}
	if _, err := os.Stat(filepath.Join(*uiDir, "index.html")); err != nil {
		logger.Printf("no UI in %s (run `make ui`): %v", *uiDir, err)
		return 1
	}
	authm, err := auth.New(c.runtimeDir)
	if err != nil {
		logger.Printf("runtime dir: %v", err)
		return 1
	}
	docs, err := store.NewDocuments(filepath.Join(*homeDir, "Documents"))
	if err != nil {
		logger.Printf("Documents: %v", err)
		return 1
	}
	pictures := filepath.Join(*homeDir, "Pictures")
	_ = os.MkdirAll(pictures, 0o700)
	records := store.OpenRecords(*dataDir, docs)
	settings := store.OpenSettings(*dataDir)
	for _, e := range []error{records.LoadError(), settings.LoadError()} {
		if e != nil {
			logger.Printf("WARNING: %v — the store is read-only until the file is repaired", e)
		}
	}

	var backend system.System
	switch *sys {
	case "fake":
		backend = system.NewFake(*fakeStorage != "blank", filepath.Join(*homeDir, ".novaos-fake-usb"))
	case "linux":
		backend = system.NewLinux(system.LinuxOptions{
			DataMount:  *dataMount,
			WebProfile: filepath.Join(*homeDir, ".local/share/novaos/web-profile"),
			RuntimeDir: c.runtimeDir,
		}, system.NewRunner("XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "SWAYSOCK", "HOME"))
	default:
		logger.Printf("unknown --system %q", *sys)
		return 2
	}
	if *dev && *sys == "linux" {
		backend = devSafe{backend} // a developer's desktop must never be shut down or have a disk formatted
	}

	handler, err := server.New(server.Options{
		Listen: c.listen, UIDir: *uiDir, DataDir: *dataDir, Auth: authm, Records: records, Settings: settings, Docs: docs,
		PicturesDir: pictures, System: backend, Weather: weather.New(brand + "/" + version + " (novad)"),
		Name: *name, Version: version, Base: osRelease(), Dev: *dev, Log: logger,
		ReloadStores: func() { records.Reload(); settings.Reload() },
	})
	if err != nil {
		logger.Print(err)
		return 1
	}
	ln, err := net.Listen("tcp", c.listen)
	if err != nil {
		logger.Printf("listen: %v", err)
		return 1
	}
	srv := &http.Server{Handler: handler, ReadHeaderTimeout: 10 * time.Second, IdleTimeout: 2 * time.Minute}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shut, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = srv.Shutdown(shut)
	}()
	logger.Printf("%s %s listening on http://%s (system backend: %s)", brand, version, c.listen, *sys)
	if *dev {
		logger.Printf("open this URL to sign in: http://%s/auth?code=%s", c.listen, authm.Code())
		logger.Printf("(after each sign-in a fresh code is written to %s; `novad login-url` prints it)", authm.CodePath())
	}
	if err := srv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
		logger.Print(err)
		return 1
	}
	return 0
}

// devSafe reads the real machine but simulates the two actions that would be destructive on a desktop.
type devSafe struct{ system.System }

func (devSafe) Power(context.Context, string) (bool, error) { return true, nil }
func (devSafe) SetupStorage(context.Context, string) error {
	return &system.UserError{Msg: "disk setup is disabled in development mode"}
}

func flagSet(fs *flag.FlagSet, name string) bool {
	set := false
	fs.Visit(func(f *flag.Flag) { set = set || f.Name == name })
	return set
}

func loginURL(args []string) int {
	fs := flag.NewFlagSet("login-url", flag.ExitOnError)
	var c common
	c.register(fs)
	fs.Parse(args)
	code, err := os.ReadFile(filepath.Join(c.runtimeDir, "login-code"))
	if err != nil {
		fmt.Fprintf(os.Stderr, "novad login-url: %v (is novad running for this user?)\n", err)
		return 1
	}
	fmt.Printf("http://%s/auth?code=%s\n", c.listen, strings.TrimSpace(string(code)))
	return 0
}

func wait(args []string) int {
	fs := flag.NewFlagSet("wait", flag.ExitOnError)
	var c common
	c.register(fs)
	timeout := fs.Duration("timeout", 30*time.Second, "how long to wait")
	fs.Parse(args)
	deadline := time.Now().Add(*timeout)
	client := &http.Client{Timeout: time.Second}
	for time.Now().Before(deadline) {
		if res, err := client.Get("http://" + c.listen + "/healthz"); err == nil {
			res.Body.Close()
			if res.StatusCode == http.StatusOK {
				return 0
			}
		}
		time.Sleep(150 * time.Millisecond)
	}
	fmt.Fprintf(os.Stderr, "novad did not answer on %s within %s\n", c.listen, *timeout)
	return 1
}
