package system

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"
)

// Fake is an in-memory device used by `novad --dev` and by tests. It validates arguments exactly like
// the Linux implementation, never touches the host, and records what it was asked to do.
type Fake struct {
	mu        sync.Mutex
	status    Status
	networks  []WifiNetwork
	known     map[string]string
	devices   []BTDevice
	zones     []string
	disks     []Disk
	usbRoot   string
	Opened    []string // URLs passed to OpenURL
	PowerLog  []string // actions passed to Power
	SetupLog  []string // disks passed to SetupStorage
	ScanDelay time.Duration
}

// NewFake returns a laptop-like fake device. usbRoot is a directory used as the mountpoint of the fake
// USB stick (it is created and given one text file).
func NewFake(persistent bool, usbRoot string) *Fake {
	signal := 72
	f := &Fake{
		status: Status{
			Battery:   &Battery{Percent: 76, Charging: false},
			Network:   Network{State: "connected", Kind: "wifi", SSID: "Nova Home", Signal: &signal},
			Wifi:      &Radio{Enabled: true},
			Bluetooth: &Radio{Enabled: false},
			Audio:     &Audio{Volume: 60},
			Backlight: &Backlight{Percent: 80, Name: "fake_backlight"},
			Time:      TimeInfo{Timezone: "UTC", NTPSynced: true, NTPEnabled: true},
			Storage:   StorageInfo{Persistent: persistent, CanSetup: !persistent},
			USB:       []USBDrive{{ID: "sdb1", Name: "sdb1", Label: "TRAVEL", SizeBytes: 16e9}},
		},
		networks: []WifiNetwork{
			{SSID: "Nova Home", Signal: 72, Security: "WPA2", Connected: true, Known: true},
			{SSID: "Café Libre", Signal: 41, Security: ""},
			{SSID: "Neighbour 5G", Signal: 23, Security: "WPA3"},
		},
		known:   map[string]string{"Nova Home": "stored"},
		devices: []BTDevice{{Address: "AA:BB:CC:00:11:22", Name: "Headphones"}, {Address: "AA:BB:CC:00:11:33", Name: "Keyboard", Paired: true}},
		zones:   []string{"UTC", "America/New_York", "Asia/Dubai", "Europe/Istanbul", "Europe/Lisbon"},
		disks:   []Disk{{Name: "vdb", SizeBytes: 8 << 30, Model: "QEMU HARDDISK"}},
		usbRoot: usbRoot,
	}
	if persistent {
		f.status.Storage.Label, f.status.Storage.Device = "NOVA-DATA", "vdb1"
		f.status.Storage.FreeBytes, f.status.Storage.TotalBytes = 7<<30, 8<<30
		f.disks = nil
	}
	if usbRoot != "" {
		_ = os.MkdirAll(usbRoot, 0o700)
		_ = os.WriteFile(filepath.Join(usbRoot, "readme.txt"), []byte("Hello from a USB stick.\n"), 0o600)
	}
	return f
}

// Log returns copies of what the fake was asked to do (never nil, so it encodes as []).
func (f *Fake) Log() (opened, power, setup []string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	cp := func(s []string) []string { return append([]string{}, s...) }
	return cp(f.Opened), cp(f.PowerLog), cp(f.SetupLog)
}

func (f *Fake) Status(context.Context) (Status, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	s := f.status
	s.USB = append([]USBDrive(nil), f.status.USB...)
	if s.Wifi != nil && !s.Wifi.Enabled && s.Network.Kind == "wifi" {
		s.Network = Network{State: "disconnected"}
	}
	return s, nil
}

func (f *Fake) Power(_ context.Context, action string) (bool, error) {
	if action != "poweroff" && action != "reboot" {
		return false, userErr("unknown power action")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	f.PowerLog = append(f.PowerLog, action)
	return true, nil
}

func (f *Fake) OpenURL(_ context.Context, raw string) error {
	u, err := ValidateURL(raw)
	if err != nil {
		return err
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	f.Opened = append(f.Opened, u)
	return nil
}

func (f *Fake) SetWifi(_ context.Context, enabled bool) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.status.Wifi.Enabled = enabled
	if enabled {
		for _, n := range f.networks {
			if n.Connected {
				sig := n.Signal
				f.status.Network = Network{State: "connected", Kind: "wifi", SSID: n.SSID, Signal: &sig}
			}
		}
	}
	return nil
}

func (f *Fake) WifiNetworks(context.Context) ([]WifiNetwork, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if !f.status.Wifi.Enabled {
		return nil, userErr("Wi-Fi is off")
	}
	out := append([]WifiNetwork(nil), f.networks...)
	sort.Slice(out, func(i, j int) bool { return out[i].Signal > out[j].Signal })
	return out, nil
}

func (f *Fake) WifiConnect(_ context.Context, ssid, password string) error {
	if err := ValidSSID(ssid); err != nil {
		return err
	}
	if err := ValidWifiPassword(password); err != nil {
		return err
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	for i := range f.networks {
		n := &f.networks[i]
		if n.SSID != ssid {
			continue
		}
		if n.Security != "" && password == "" && f.known[ssid] == "" {
			return userErr("a password is required")
		}
		for j := range f.networks {
			f.networks[j].Connected = false
		}
		n.Connected, n.Known = true, true
		f.known[ssid] = "stored"
		sig := n.Signal
		f.status.Network = Network{State: "connected", Kind: "wifi", SSID: ssid, Signal: &sig}
		return nil
	}
	return userErr("network not found")
}

func (f *Fake) WifiDisconnect(context.Context) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	for i := range f.networks {
		f.networks[i].Connected = false
	}
	f.status.Network = Network{State: "disconnected"}
	return nil
}

func (f *Fake) SetBluetooth(_ context.Context, enabled bool) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.status.Bluetooth.Enabled = enabled
	return nil
}

func (f *Fake) BluetoothDevices(ctx context.Context) ([]BTDevice, error) {
	f.mu.Lock()
	enabled, delay := f.status.Bluetooth.Enabled, f.ScanDelay
	f.mu.Unlock()
	if !enabled {
		return nil, userErr("Bluetooth is off")
	}
	select {
	case <-time.After(delay):
	case <-ctx.Done():
		return nil, ctx.Err()
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]BTDevice(nil), f.devices...), nil
}

func (f *Fake) BluetoothAction(_ context.Context, action, address string) error {
	if !ValidMAC(address) {
		return userErr("invalid device address")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	for i := range f.devices {
		d := &f.devices[i]
		if d.Address != address {
			continue
		}
		switch action {
		case "pair":
			d.Paired = true
		case "connect":
			if !d.Paired {
				return userErr("pair the device first")
			}
			d.Connected = true
		case "disconnect":
			d.Connected = false
		case "remove":
			d.Paired, d.Connected = false, false
		default:
			return userErr("unknown Bluetooth action")
		}
		return nil
	}
	return userErr("device not found")
}

func (f *Fake) SetAudio(_ context.Context, volume *int, muted *bool) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if volume != nil {
		if *volume < 0 || *volume > 100 {
			return userErr("volume must be between 0 and 100")
		}
		f.status.Audio.Volume = *volume
	}
	if muted != nil {
		f.status.Audio.Muted = *muted
	}
	return nil
}

func (f *Fake) SetBacklight(_ context.Context, percent int) error {
	if percent < 1 || percent > 100 {
		return userErr("brightness must be between 1 and 100")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	f.status.Backlight.Percent = percent
	return nil
}

func (f *Fake) Timezones(context.Context) ([]string, string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]string(nil), f.zones...), f.status.Time.Timezone, nil
}

func (f *Fake) SetTimezone(_ context.Context, zone string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	for _, z := range f.zones {
		if z == zone && ValidZoneSyntax(zone) {
			f.status.Time.Timezone = zone
			return nil
		}
	}
	return userErr("unknown time zone")
}

func (f *Fake) StorageCandidates(context.Context) ([]Disk, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]Disk(nil), f.disks...), nil
}

func (f *Fake) SetupStorage(_ context.Context, disk string) error {
	if !ValidDiskName(disk) {
		return userErr("invalid disk name")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	for _, d := range f.disks {
		if d.Name == disk {
			f.SetupLog = append(f.SetupLog, disk)
			f.status.Storage = StorageInfo{Persistent: true, Label: "NOVA-DATA", Device: disk + "1", FreeBytes: d.SizeBytes - 1<<28, TotalBytes: d.SizeBytes}
			f.disks = nil
			return nil
		}
	}
	return userErr("that disk is not empty or no longer attached")
}

func (f *Fake) usb(id string) (*USBDrive, error) {
	if !ValidBlockName(id) {
		return nil, userErr("invalid drive")
	}
	for i := range f.status.USB {
		if f.status.USB[i].ID == id {
			return &f.status.USB[i], nil
		}
	}
	return nil, userErr("that drive is no longer connected")
}

func (f *Fake) USBMount(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	d, err := f.usb(id)
	if err != nil {
		return err
	}
	d.Mounted, d.Mountpoint = true, f.usbRoot
	return nil
}

func (f *Fake) USBUnmount(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	d, err := f.usb(id)
	if err != nil {
		return err
	}
	d.Mounted, d.Mountpoint = false, ""
	return nil
}

func (f *Fake) USBMountpoint(_ context.Context, id string) (string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	d, err := f.usb(id)
	if err != nil {
		return "", err
	}
	if !d.Mounted || d.Mountpoint == "" {
		return "", errors.New("the drive is not open")
	}
	return d.Mountpoint, nil
}
