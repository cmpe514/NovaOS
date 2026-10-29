package system

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// LinuxOptions configures the appliance backend.
type LinuxOptions struct {
	// DataMount is where the NOVA-DATA partition is mounted (/var/lib/novaos). When nothing is mounted
	// there, data lives in RAM and Status reports Persistent=false.
	DataMount string
	// WebProfile is the Chromium user-data-dir for the Browser app (never the shell's profile).
	WebProfile string
	// RuntimeDir holds short-lived secrets (Wi-Fi password files), mode 0700.
	RuntimeDir string
	// StorageUnit is the privileged template unit that formats a blank disk ("novaos-storage-setup@").
	StorageUnit string
	// SysRoot is "/" except in tests.
	SysRoot string
}

// Linux drives the real device through allow-listed tools. See SECURITY.md for the privilege model:
// novad runs as the unprivileged session user; polkit grants that user (only while its session is
// active) exactly the actions used below.
type Linux struct {
	opt   LinuxOptions
	run   *Runner
	mu    sync.Mutex
	zones []string
}

// NewLinux returns the appliance backend.
func NewLinux(opt LinuxOptions, runner *Runner) *Linux {
	if opt.SysRoot == "" {
		opt.SysRoot = "/"
	}
	if opt.StorageUnit == "" {
		opt.StorageUnit = "novaos-storage-setup@"
	}
	return &Linux{opt: opt, run: runner}
}

func (l *Linux) sys(p ...string) string {
	return filepath.Join(append([]string{l.opt.SysRoot}, p...)...)
}

func readTrim(path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(b))
}

// ---------- status ----------

func (l *Linux) Status(ctx context.Context) (Status, error) {
	var st Status
	st.Battery = l.battery()
	st.Backlight = l.backlight()
	st.Network, st.Wifi = l.network(ctx)
	st.Bluetooth = l.bluetooth(ctx)
	st.Audio = l.audio(ctx)
	st.Time = l.timeInfo(ctx)
	st.Storage = l.storage()
	if devs, err := l.lsblk(ctx); err == nil {
		st.USB = usbDrives(devs)
	}
	return st, nil
}

func (l *Linux) battery() *Battery {
	entries, _ := os.ReadDir(l.sys("sys/class/power_supply"))
	for _, e := range entries {
		dir := l.sys("sys/class/power_supply", e.Name())
		if readTrim(filepath.Join(dir, "type")) != "Battery" || readTrim(filepath.Join(dir, "scope")) == "Device" {
			continue // skip mice, keyboards and other peripherals' batteries
		}
		pct, err := strconv.Atoi(readTrim(filepath.Join(dir, "capacity")))
		if err != nil {
			continue
		}
		status := readTrim(filepath.Join(dir, "status"))
		return &Battery{Percent: max(0, min(100, pct)), Charging: status == "Charging"}
	}
	return nil
}

func (l *Linux) backlightDev() (name string, cur, maxv int, ok bool) {
	entries, _ := os.ReadDir(l.sys("sys/class/backlight"))
	names := []string{}
	for _, e := range entries {
		names = append(names, e.Name())
	}
	// Prefer firmware/platform interfaces over raw ones, as the kernel documentation recommends.
	rank := func(n string) int {
		switch readTrim(l.sys("sys/class/backlight", n, "type")) {
		case "firmware":
			return 0
		case "platform":
			return 1
		}
		return 2
	}
	sort.SliceStable(names, func(i, j int) bool { return rank(names[i]) < rank(names[j]) })
	for _, n := range names {
		if !ValidBacklightName(n) {
			continue
		}
		c, err1 := strconv.Atoi(readTrim(l.sys("sys/class/backlight", n, "brightness")))
		m, err2 := strconv.Atoi(readTrim(l.sys("sys/class/backlight", n, "max_brightness")))
		if err1 == nil && err2 == nil && m > 0 {
			return n, c, m, true
		}
	}
	return "", 0, 0, false
}

func (l *Linux) backlight() *Backlight {
	name, cur, maxv, ok := l.backlightDev()
	if !ok {
		return nil
	}
	return &Backlight{Name: name, Percent: (cur*100 + maxv/2) / maxv}
}

func (l *Linux) network(ctx context.Context) (Network, *Radio) {
	if !l.run.Has("nmcli") {
		return Network{State: "unavailable"}, nil
	}
	out, err := l.run.Run(ctx, "nmcli", "-t", "-f", "TYPE,STATE,CONNECTION,DEVICE", "device", "status")
	if err != nil {
		return Network{State: "unavailable"}, nil
	}
	var wifi *Radio
	n := Network{State: "disconnected"}
	var wifiConnected bool
	for _, d := range parseDeviceStatus(out) {
		switch d.Type {
		case "wifi":
			if wifi == nil {
				wifi = &Radio{Enabled: d.State != "unavailable"}
			}
			if strings.HasPrefix(d.State, "connected") {
				wifiConnected = true
			} else if strings.HasPrefix(d.State, "connecting") && n.State != "connected" {
				n.State = "connecting"
			}
		case "ethernet":
			if strings.HasPrefix(d.State, "connected") {
				n = Network{State: "connected", Kind: "ethernet"}
			} else if strings.HasPrefix(d.State, "connecting") && n.State != "connected" {
				n.State = "connecting"
			}
		}
	}
	if wifi != nil {
		if radio, err := l.run.Run(ctx, "nmcli", "radio", "wifi"); err == nil {
			wifi.Enabled = strings.TrimSpace(string(radio)) == "enabled"
		}
	}
	if wifiConnected && n.Kind != "ethernet" {
		n = Network{State: "connected", Kind: "wifi"}
		if list, err := l.run.Run(ctx, "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "no"); err == nil {
			for _, w := range parseWifiList(list, nil) {
				if w.Connected {
					sig := w.Signal
					n.SSID, n.Signal = w.SSID, &sig
				}
			}
		}
	}
	return n, wifi
}

func (l *Linux) bluetooth(ctx context.Context) *Radio {
	if m, _ := filepath.Glob(l.sys("sys/class/bluetooth/hci*")); len(m) == 0 || !l.run.Has("bluetoothctl") {
		return nil
	}
	out, err := l.run.Run(ctx, "bluetoothctl", "show")
	if err != nil {
		return &Radio{Enabled: false}
	}
	return &Radio{Enabled: parseKeyValue(out, ":")["Powered"] == "yes"}
}

func (l *Linux) audio(ctx context.Context) *Audio {
	if !l.run.Has("wpctl") {
		return nil
	}
	out, err := l.run.Run(ctx, "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@")
	if err != nil {
		return nil
	}
	a, ok := parseWpctlVolume(out)
	if !ok {
		return nil
	}
	return a
}

func (l *Linux) timeInfo(ctx context.Context) TimeInfo {
	t := TimeInfo{Timezone: "UTC"}
	if out, err := l.run.Run(ctx, "timedatectl", "show", "-p", "Timezone", "-p", "NTP", "-p", "NTPSynchronized"); err == nil {
		kv := parseKeyValue(out, "=")
		t.Timezone = firstNonEmpty(kv["Timezone"], "UTC")
		t.NTPEnabled = kv["NTP"] == "yes"
		t.NTPSynced = kv["NTPSynchronized"] == "yes"
	}
	return t
}

// storage reports whether DataMount is a mounted NOVA-DATA filesystem.
func (l *Linux) storage() StorageInfo {
	info := StorageInfo{}
	_, unitErr := os.Stat(l.sys("usr/lib/systemd/system", l.opt.StorageUnit+".service"))
	src, ok := mountSource(l.sys("proc/self/mountinfo"), l.opt.DataMount)
	if ok {
		if label := l.labelDevice("NOVA-DATA"); label != "" && label == src {
			info.Persistent, info.Label, info.Device = true, "NOVA-DATA", filepath.Base(src)
			var fs syscall.Statfs_t
			if syscall.Statfs(l.opt.DataMount, &fs) == nil {
				info.FreeBytes = fs.Bavail * uint64(fs.Bsize)
				info.TotalBytes = fs.Blocks * uint64(fs.Bsize)
			}
		}
	}
	info.CanSetup = !info.Persistent && unitErr == nil && l.run.Has("systemctl")
	return info
}

// labelDevice resolves /dev/disk/by-label/<label> to a device path such as /dev/vdb1 (udev keeps these
// links relative: ../../vdb1).
func (l *Linux) labelDevice(label string) string {
	link := l.sys("dev/disk/by-label", label)
	target, err := os.Readlink(link)
	if err != nil {
		return ""
	}
	if !filepath.IsAbs(target) {
		target = filepath.Join(filepath.Dir(link), target)
	} else {
		target = l.sys(target)
	}
	rel, err := filepath.Rel(l.opt.SysRoot, filepath.Clean(target))
	if err != nil || strings.HasPrefix(rel, "..") {
		return ""
	}
	return "/" + filepath.ToSlash(rel)
}

// mountSource returns the source device of the filesystem mounted exactly at target.
func mountSource(mountinfo, target string) (string, bool) {
	data, err := os.ReadFile(mountinfo)
	if err != nil {
		return "", false
	}
	src, found := "", false
	for _, line := range strings.Split(string(data), "\n") {
		// id parent major:minor root mountpoint options [optional...] - fstype source superopts
		left, right, ok := strings.Cut(line, " - ")
		if !ok {
			continue
		}
		lf, rf := strings.Fields(left), strings.Fields(right)
		if len(lf) < 5 || len(rf) < 2 {
			continue
		}
		if unescapeMount(lf[4]) == target {
			src, found = rf[1], true // the last matching line wins (stacked mounts)
		}
	}
	return src, found
}

func unescapeMount(s string) string {
	return strings.NewReplacer(`\040`, " ", `\011`, "\t", `\012`, "\n", `\134`, `\`).Replace(s)
}

func (l *Linux) lsblk(ctx context.Context) ([]blockDev, error) {
	out, err := l.run.Run(ctx, "lsblk", "-J", "-b", "-o", lsblkColumns)
	if err != nil {
		return nil, err
	}
	return parseLsblk(out)
}

// ---------- power and browser ----------

func (l *Linux) Power(ctx context.Context, action string) (bool, error) {
	if action != "poweroff" && action != "reboot" {
		return false, userErr("unknown power action")
	}
	// systemd stops the session, flushes and unmounts every filesystem (including NOVA-DATA) cleanly.
	_, err := l.run.Run(ctx, "systemctl", action)
	return false, err
}

var appIDRE = regexp.MustCompile(`^[a-z][a-z0-9-]{0,31}$`)

func (l *Linux) OpenURL(ctx context.Context, raw string) error {
	u, err := ValidateURL(raw)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(l.opt.WebProfile, 0o700); err != nil {
		return err
	}
	args := []string{
		"--user-data-dir=" + l.opt.WebProfile,
		"--class=novaos-web",
		"--ozone-platform=wayland",
		"--enable-wayland-ime",
		"--no-first-run",
		"--no-default-browser-check",
		"--password-store=basic",
		"--start-maximized",
		"--", u,
	}
	if err := l.run.Start("chromium", args...); err != nil {
		return fmt.Errorf("could not start the browser: %w", err)
	}
	// If the browser was already running, Chromium hands the URL to it and exits; bring it forward.
	if l.run.Has("swaymsg") && appIDRE.MatchString("novaos-web") {
		go func() {
			time.Sleep(400 * time.Millisecond)
			c, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			_, _ = l.run.Run(c, "swaymsg", `[app_id="^novaos-web$"]`, "focus")
		}()
	}
	return nil
}

// ---------- Wi-Fi (NetworkManager) ----------

func (l *Linux) SetWifi(ctx context.Context, enabled bool) error {
	state := "off"
	if enabled {
		state = "on"
	}
	_, err := l.run.Run(ctx, "nmcli", "radio", "wifi", state)
	return err
}

func (l *Linux) knownWifi(ctx context.Context) map[string]bool {
	known := map[string]bool{}
	out, err := l.run.Run(ctx, "nmcli", "-t", "-f", "NAME,TYPE", "connection", "show")
	if err != nil {
		return known
	}
	for _, line := range lines(out) {
		if f := splitTerse(line); len(f) == 2 && f[1] == "802-11-wireless" {
			known[f[0]] = true
		}
	}
	return known
}

func (l *Linux) WifiNetworks(ctx context.Context) ([]WifiNetwork, error) {
	out, err := l.run.Run(ctx, "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "yes")
	if err != nil {
		return nil, userErr("could not scan for networks (is Wi-Fi on?)")
	}
	nets := parseWifiList(out, l.knownWifi(ctx))
	sort.SliceStable(nets, func(i, j int) bool {
		if nets[i].Connected != nets[j].Connected {
			return nets[i].Connected
		}
		return nets[i].Signal > nets[j].Signal
	})
	if len(nets) > 40 {
		nets = nets[:40]
	}
	return nets, nil
}

func (l *Linux) WifiConnect(ctx context.Context, ssid, password string) error {
	if err := ValidSSID(ssid); err != nil {
		return err
	}
	if err := ValidWifiPassword(password); err != nil {
		return err
	}
	if password == "" {
		if l.knownWifi(ctx)[ssid] {
			_, err := l.run.Run(ctx, "nmcli", "connection", "up", "id", ssid)
			return friendlyNM(err)
		}
		_, err := l.run.Run(ctx, "nmcli", "device", "wifi", "connect", ssid)
		return friendlyNM(err)
	}
	// The password never appears on a command line: NetworkManager reads it from a 0600 file.
	keyMgmt := "wpa-psk"
	if nets, err := l.WifiNetworks(ctx); err == nil {
		for _, n := range nets {
			if n.SSID == ssid && strings.Contains(n.Security, "WPA3") && !strings.Contains(n.Security, "WPA2") {
				keyMgmt = "sae"
			}
		}
	}
	name := ssid
	if l.knownWifi(ctx)[name] {
		_, _ = l.run.Run(ctx, "nmcli", "connection", "delete", "id", name) // replace a profile with a stale password
	}
	if _, err := l.run.Run(ctx, "nmcli", "connection", "add", "type", "wifi", "con-name", name, "ssid", ssid,
		"wifi-sec.key-mgmt", keyMgmt, "wifi-sec.psk-flags", "0"); err != nil {
		return friendlyNM(err)
	}
	file, err := os.CreateTemp(l.opt.RuntimeDir, ".wifi-*")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	_, werr := fmt.Fprintf(file, "802-11-wireless-security.psk:%s\n", password)
	cerr := file.Close()
	if werr != nil || cerr != nil {
		return errors.New("could not prepare the password")
	}
	// psk-flags=0 makes the secret system-owned, so NetworkManager stores the password it receives here
	// in the profile and rejoins the network automatically next time.
	if _, err := l.run.Run(ctx, "nmcli", "connection", "up", "id", name, "passwd-file", file.Name()); err != nil {
		_, _ = l.run.Run(context.Background(), "nmcli", "connection", "delete", "id", name) // no half-configured profile
		return friendlyNM(err)
	}
	return nil
}

func friendlyNM(err error) error {
	if err == nil {
		return nil
	}
	msg := err.Error()
	switch {
	case strings.Contains(msg, "Secrets were required") || strings.Contains(msg, "802-1X") || strings.Contains(msg, "psk"):
		return userErr("the password was not accepted")
	case strings.Contains(msg, "No network with SSID"):
		return userErr("that network is out of range")
	}
	return userErr("could not connect: " + strings.TrimPrefix(msg, "nmcli failed: "))
}

var ifaceRE = regexp.MustCompile(`^[A-Za-z0-9._-]{1,15}$`)

func (l *Linux) WifiDisconnect(ctx context.Context) error {
	out, err := l.run.Run(ctx, "nmcli", "-t", "-f", "TYPE,STATE,CONNECTION,DEVICE", "device", "status")
	if err != nil {
		return err
	}
	for _, d := range parseDeviceStatus(out) {
		if d.Type == "wifi" && strings.HasPrefix(d.State, "connected") && ifaceRE.MatchString(d.Iface) {
			_, err := l.run.Run(ctx, "nmcli", "device", "disconnect", d.Iface)
			return err
		}
	}
	return nil
}

// ---------- Bluetooth (BlueZ) ----------

func (l *Linux) SetBluetooth(ctx context.Context, enabled bool) error {
	if enabled {
		if l.run.Has("rfkill") {
			_, _ = l.run.Run(ctx, "rfkill", "unblock", "bluetooth")
		}
		_, err := l.run.Run(ctx, "bluetoothctl", "power", "on")
		return err
	}
	_, err := l.run.Run(ctx, "bluetoothctl", "power", "off")
	return err
}

func (l *Linux) BluetoothDevices(ctx context.Context) ([]BTDevice, error) {
	if r := l.bluetooth(ctx); r == nil {
		return nil, ErrUnsupported
	} else if !r.Enabled {
		return nil, userErr("Bluetooth is off")
	}
	_, _ = l.run.Run(ctx, "bluetoothctl", "--timeout", "6", "scan", "on")
	out, err := l.run.Run(ctx, "bluetoothctl", "devices")
	if err != nil {
		return nil, err
	}
	devs := parseBluetoothDevices(out)
	if len(devs) > 30 {
		devs = devs[:30]
	}
	for i := range devs {
		if info, err := l.run.Run(ctx, "bluetoothctl", "info", devs[i].Address); err == nil {
			kv := parseKeyValue(info, ":")
			devs[i].Paired = kv["Paired"] == "yes"
			devs[i].Connected = kv["Connected"] == "yes"
			if devs[i].Name == "" {
				devs[i].Name = kv["Alias"]
			}
		}
	}
	sort.SliceStable(devs, func(i, j int) bool { return devs[i].Paired && !devs[j].Paired })
	return devs, nil
}

func (l *Linux) BluetoothAction(ctx context.Context, action, address string) error {
	if !ValidMAC(address) {
		return userErr("invalid device address")
	}
	var args [][]string
	switch action {
	case "pair":
		args = [][]string{{"--timeout", "25", "pair", address}, {"trust", address}}
	case "connect":
		args = [][]string{{"--timeout", "15", "connect", address}}
	case "disconnect":
		args = [][]string{{"disconnect", address}}
	case "remove":
		args = [][]string{{"remove", address}}
	default:
		return userErr("unknown Bluetooth action")
	}
	for _, a := range args {
		out, err := l.run.Run(ctx, "bluetoothctl", a...)
		if err != nil || strings.Contains(string(out), "Failed to") {
			return userErr(action + " failed — make sure the device is nearby and in pairing mode")
		}
	}
	return nil
}

// ---------- sound and backlight ----------

func (l *Linux) SetAudio(ctx context.Context, volume *int, muted *bool) error {
	if l.audio(ctx) == nil {
		return ErrUnsupported
	}
	if volume != nil {
		if *volume < 0 || *volume > 100 {
			return userErr("volume must be between 0 and 100")
		}
		if _, err := l.run.Run(ctx, "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", fmt.Sprintf("%.2f", float64(*volume)/100)); err != nil {
			return err
		}
	}
	if muted != nil {
		v := "0"
		if *muted {
			v = "1"
		}
		if _, err := l.run.Run(ctx, "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", v); err != nil {
			return err
		}
	}
	return nil
}

func (l *Linux) SetBacklight(ctx context.Context, percent int) error {
	if percent < 1 || percent > 100 {
		return userErr("brightness must be between 1 and 100")
	}
	name, _, maxv, ok := l.backlightDev()
	if !ok {
		return ErrUnsupported
	}
	value := (percent*maxv + 50) / 100
	if value < 1 {
		value = 1
	}
	// logind lets the owner of the active session set the backlight without any extra privilege.
	_, err := l.run.Run(ctx, "busctl", "call", "org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
		"org.freedesktop.login1.Session", "SetBrightness", "ssu", "backlight", name, strconv.Itoa(value))
	return err
}

// ---------- time ----------

func (l *Linux) Timezones(ctx context.Context) ([]string, string, error) {
	l.mu.Lock()
	zones := l.zones
	l.mu.Unlock()
	if zones == nil {
		out, err := l.run.Run(ctx, "timedatectl", "list-timezones")
		if err != nil {
			return nil, "", err
		}
		for _, z := range lines(out) {
			if ValidZoneSyntax(z) {
				zones = append(zones, z)
			}
		}
		l.mu.Lock()
		l.zones = zones
		l.mu.Unlock()
	}
	return zones, l.timeInfo(ctx).Timezone, nil
}

func (l *Linux) SetTimezone(ctx context.Context, zone string) error {
	zones, _, err := l.Timezones(ctx)
	if err != nil {
		return err
	}
	for _, z := range zones {
		if z == zone {
			_, err := l.run.Run(ctx, "timedatectl", "set-timezone", zone)
			return err
		}
	}
	return userErr("unknown time zone")
}

// ---------- storage ----------

func (l *Linux) StorageCandidates(ctx context.Context) ([]Disk, error) {
	if l.storage().Persistent {
		return nil, nil
	}
	devs, err := l.lsblk(ctx)
	if err != nil {
		return nil, err
	}
	return blankDisks(devs), nil
}

func (l *Linux) SetupStorage(ctx context.Context, disk string) error {
	if !ValidDiskName(disk) {
		return userErr("invalid disk name")
	}
	// The privileged helper re-checks that the disk is blank before it writes anything.
	_, err := l.run.Run(ctx, "systemctl", "start", l.opt.StorageUnit+disk+".service")
	if err != nil {
		return userErr("storage setup failed; the disk was left unchanged if it was not blank (details are in the system log)")
	}
	if !l.storage().Persistent {
		return userErr("the disk was prepared but could not be mounted; restart to try again")
	}
	return nil
}

// ---------- USB drives (udisks2) ----------

func (l *Linux) usbByID(ctx context.Context, id string) (USBDrive, error) {
	if !ValidBlockName(id) {
		return USBDrive{}, userErr("invalid drive")
	}
	devs, err := l.lsblk(ctx)
	if err != nil {
		return USBDrive{}, err
	}
	for _, d := range usbDrives(devs) {
		if d.ID == id {
			return d, nil
		}
	}
	return USBDrive{}, userErr("that drive is no longer connected")
}

func (l *Linux) USBMount(ctx context.Context, id string) error {
	if _, err := l.usbByID(ctx, id); err != nil {
		return err
	}
	_, err := l.run.Run(ctx, "udisksctl", "mount", "--no-user-interaction", "-b", "/dev/"+id)
	return err
}

func (l *Linux) USBUnmount(ctx context.Context, id string) error {
	d, err := l.usbByID(ctx, id)
	if err != nil {
		return err
	}
	if d.Mounted {
		if _, err := l.run.Run(ctx, "udisksctl", "unmount", "--no-user-interaction", "-b", "/dev/"+id); err != nil {
			return userErr("the drive is busy; close anything using it and try again")
		}
	}
	return nil
}

func (l *Linux) USBMountpoint(ctx context.Context, id string) (string, error) {
	d, err := l.usbByID(ctx, id)
	if err != nil {
		return "", err
	}
	if !d.Mounted {
		return "", userErr("open the drive first")
	}
	return d.Mountpoint, nil
}
