// Package system is novad's window onto the device: power, network, radios, sound, backlight, time,
// storage and USB drives. The Linux implementation drives standard tools (systemctl, nmcli, bluetoothctl,
// wpctl, timedatectl, lsblk, udisksctl) through an allow-listed runner; the fake one backs `novad --dev`
// and the tests.
package system

import (
	"context"
	"errors"
)

// ErrUnsupported means the device lacks the hardware or service for a request.
var ErrUnsupported = errors.New("not available on this device")

// UserError carries a message that is safe and useful to show in the UI.
type UserError struct{ Msg string }

func (e *UserError) Error() string { return e.Msg }

func userErr(msg string) error { return &UserError{Msg: msg} }

type Battery struct {
	Percent  int  `json:"percent"`
	Charging bool `json:"charging"`
}

type Network struct {
	State  string `json:"state"`            // connected | connecting | disconnected | unavailable
	Kind   string `json:"kind,omitempty"`   // wifi | ethernet
	SSID   string `json:"ssid,omitempty"`   // when Kind is wifi
	Signal *int   `json:"signal,omitempty"` // 0–100, when Kind is wifi
}

type Radio struct {
	Enabled bool `json:"enabled"`
}

type Audio struct {
	Volume int  `json:"volume"` // 0–100
	Muted  bool `json:"muted"`
}

type Backlight struct {
	Percent int    `json:"percent"`
	Name    string `json:"-"`
}

type TimeInfo struct {
	Timezone   string `json:"timezone"`
	NTPSynced  bool   `json:"ntpSynced"`
	NTPEnabled bool   `json:"ntpEnabled"`
}

type StorageInfo struct {
	Persistent bool   `json:"persistent"`
	Label      string `json:"label,omitempty"`
	Device     string `json:"device,omitempty"`
	FreeBytes  uint64 `json:"freeBytes,omitempty"`
	TotalBytes uint64 `json:"totalBytes,omitempty"`
	CanSetup   bool   `json:"canSetup"`
}

type USBDrive struct {
	ID         string `json:"id"` // kernel name of the partition, e.g. sdb1
	Name       string `json:"name"`
	Label      string `json:"label,omitempty"`
	SizeBytes  uint64 `json:"sizeBytes,omitempty"`
	Mounted    bool   `json:"mounted"`
	Mountpoint string `json:"-"`
}

// Status is everything the status bar and Settings show. A nil pointer means "this device has none".
type Status struct {
	Battery   *Battery    `json:"battery"`
	Network   Network     `json:"network"`
	Wifi      *Radio      `json:"wifi"`
	Bluetooth *Radio      `json:"bluetooth"`
	Audio     *Audio      `json:"audio"`
	Backlight *Backlight  `json:"backlight"`
	Time      TimeInfo    `json:"time"`
	Storage   StorageInfo `json:"storage"`
	USB       []USBDrive  `json:"usb"`
}

type WifiNetwork struct {
	SSID      string `json:"ssid"`
	Signal    int    `json:"signal"`
	Security  string `json:"security"`
	Connected bool   `json:"connected"`
	Known     bool   `json:"known"`
}

type BTDevice struct {
	Address   string `json:"address"`
	Name      string `json:"name"`
	Paired    bool   `json:"paired"`
	Connected bool   `json:"connected"`
}

// Disk is a candidate for the NOVA-DATA partition: a whole, completely blank disk.
type Disk struct {
	Name      string `json:"name"` // kernel name, e.g. vdb
	SizeBytes uint64 `json:"sizeBytes"`
	Model     string `json:"model,omitempty"`
}

// System is implemented by Linux (the appliance) and Fake (development and tests).
type System interface {
	Status(ctx context.Context) (Status, error)
	// Power performs "poweroff" or "reboot". dryRun is true when nothing was actually done.
	Power(ctx context.Context, action string) (dryRun bool, err error)
	OpenURL(ctx context.Context, url string) error
	SetWifi(ctx context.Context, enabled bool) error
	WifiNetworks(ctx context.Context) ([]WifiNetwork, error)
	WifiConnect(ctx context.Context, ssid, password string) error
	WifiDisconnect(ctx context.Context) error
	SetBluetooth(ctx context.Context, enabled bool) error
	BluetoothDevices(ctx context.Context) ([]BTDevice, error)
	// BluetoothAction is "pair", "connect", "disconnect" or "remove".
	BluetoothAction(ctx context.Context, action, address string) error
	SetAudio(ctx context.Context, volume *int, muted *bool) error
	SetBacklight(ctx context.Context, percent int) error
	Timezones(ctx context.Context) (zones []string, current string, err error)
	SetTimezone(ctx context.Context, zone string) error
	StorageCandidates(ctx context.Context) ([]Disk, error)
	SetupStorage(ctx context.Context, disk string) error
	USBMount(ctx context.Context, id string) error
	USBUnmount(ctx context.Context, id string) error
	// USBMountpoint returns where a mounted USB partition is, for listing its text files.
	USBMountpoint(ctx context.Context, id string) (string, error)
}
