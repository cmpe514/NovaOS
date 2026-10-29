package system

import (
	"bufio"
	"bytes"
	"encoding/json"
	"strconv"
	"strings"
)

// splitTerse splits one line of `nmcli -t` output. nmcli separates fields with ':' and escapes literal
// ':' and '\' inside values with a backslash.
func splitTerse(line string) []string {
	var fields []string
	var cur strings.Builder
	escaped := false
	for _, r := range line {
		switch {
		case escaped:
			cur.WriteRune(r)
			escaped = false
		case r == '\\':
			escaped = true
		case r == ':':
			fields = append(fields, cur.String())
			cur.Reset()
		default:
			cur.WriteRune(r)
		}
	}
	return append(fields, cur.String())
}

func lines(out []byte) []string {
	var res []string
	sc := bufio.NewScanner(bytes.NewReader(out))
	sc.Buffer(make([]byte, 64*1024), 1<<20)
	for sc.Scan() {
		if l := strings.TrimRight(sc.Text(), "\r"); l != "" {
			res = append(res, l)
		}
	}
	return res
}

type nmDevice struct{ Type, State, Connection, Iface string }

// parseDeviceStatus parses `nmcli -t -f TYPE,STATE,CONNECTION,DEVICE device status`.
func parseDeviceStatus(out []byte) []nmDevice {
	var devs []nmDevice
	for _, l := range lines(out) {
		f := splitTerse(l)
		if len(f) == 4 {
			devs = append(devs, nmDevice{Type: f[0], State: f[1], Connection: f[2], Iface: f[3]})
		}
	}
	return devs
}

// parseWifiList parses `nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list`, merging duplicate
// SSIDs (several access points) and dropping hidden networks.
func parseWifiList(out []byte, known map[string]bool) []WifiNetwork {
	index := map[string]int{}
	var nets []WifiNetwork
	for _, l := range lines(out) {
		f := splitTerse(l)
		if len(f) != 4 || f[1] == "" || f[1] == "--" {
			continue
		}
		signal, _ := strconv.Atoi(f[2])
		sec := f[3]
		if sec == "--" {
			sec = ""
		}
		n := WifiNetwork{SSID: f[1], Signal: signal, Security: sec, Connected: strings.TrimSpace(f[0]) == "*", Known: known[f[1]]}
		if i, ok := index[n.SSID]; ok {
			if n.Signal > nets[i].Signal {
				nets[i].Signal = n.Signal
			}
			nets[i].Connected = nets[i].Connected || n.Connected
			continue
		}
		index[n.SSID] = len(nets)
		nets = append(nets, n)
	}
	return nets
}

// parseWpctlVolume parses `wpctl get-volume @DEFAULT_AUDIO_SINK@`, e.g. "Volume: 0.40 [MUTED]".
func parseWpctlVolume(out []byte) (*Audio, bool) {
	s := strings.TrimSpace(string(out))
	if !strings.HasPrefix(s, "Volume:") {
		return nil, false
	}
	fields := strings.Fields(strings.TrimPrefix(s, "Volume:"))
	if len(fields) == 0 {
		return nil, false
	}
	v, err := strconv.ParseFloat(fields[0], 64)
	if err != nil {
		return nil, false
	}
	pct := int(v*100 + 0.5)
	if pct > 100 {
		pct = 100
	}
	return &Audio{Volume: pct, Muted: strings.Contains(s, "[MUTED]")}, true
}

// parseKeyValue parses KEY=value lines (timedatectl show, bluetoothctl show uses "Key: value").
func parseKeyValue(out []byte, sep string) map[string]string {
	m := map[string]string{}
	for _, l := range lines(out) {
		if k, v, ok := strings.Cut(strings.TrimSpace(l), sep); ok {
			m[strings.TrimSpace(k)] = strings.TrimSpace(v)
		}
	}
	return m
}

// parseBluetoothDevices parses `bluetoothctl devices`: "Device AA:BB:CC:DD:EE:FF Name".
func parseBluetoothDevices(out []byte) []BTDevice {
	var devs []BTDevice
	seen := map[string]bool{}
	for _, l := range lines(out) {
		f := strings.SplitN(strings.TrimSpace(l), " ", 3)
		if len(f) < 2 || f[0] != "Device" || !ValidMAC(f[1]) || seen[f[1]] {
			continue
		}
		seen[f[1]] = true
		d := BTDevice{Address: f[1]}
		if len(f) == 3 {
			d.Name = f[2]
		}
		if d.Name == strings.ReplaceAll(d.Address, ":", "-") {
			d.Name = "" // bluetoothctl uses the address as name when it has none
		}
		devs = append(devs, d)
	}
	return devs
}

// flexBool accepts JSON true/false and the "0"/"1" strings older lsblk versions print.
type flexBool bool

func (b *flexBool) UnmarshalJSON(data []byte) error {
	s := strings.Trim(string(data), `"`)
	*b = flexBool(s == "true" || s == "1")
	return nil
}

// flexUint accepts numbers and numeric strings.
type flexUint uint64

func (u *flexUint) UnmarshalJSON(data []byte) error {
	s := strings.Trim(string(data), `"`)
	if s == "null" || s == "" {
		*u = 0
		return nil
	}
	v, err := strconv.ParseUint(s, 10, 64)
	*u = flexUint(v)
	return err
}

type blockDev struct {
	Name        string     `json:"name"`
	Kname       string     `json:"kname"`
	Path        string     `json:"path"`
	Size        flexUint   `json:"size"`
	Type        string     `json:"type"`
	RO          flexBool   `json:"ro"`
	RM          flexBool   `json:"rm"`
	Hotplug     flexBool   `json:"hotplug"`
	Tran        *string    `json:"tran"`
	Model       *string    `json:"model"`
	Fstype      *string    `json:"fstype"`
	Pttype      *string    `json:"pttype"`
	Label       *string    `json:"label"`
	Mountpoints []*string  `json:"mountpoints"`
	Mountpoint  *string    `json:"mountpoint"` // util-linux < 2.37
	Children    []blockDev `json:"children"`
}

func str(p *string) string {
	if p == nil {
		return ""
	}
	return strings.TrimSpace(*p)
}

func (d blockDev) mountpoints() []string {
	var out []string
	for _, m := range d.Mountpoints {
		if m != nil && *m != "" {
			out = append(out, *m)
		}
	}
	if len(out) == 0 && d.Mountpoint != nil && *d.Mountpoint != "" {
		out = append(out, *d.Mountpoint)
	}
	return out
}

func (d blockDev) kernelName() string { return firstNonEmpty(d.Kname, d.Name) }

// lsblkColumns is the column list every lsblk call uses.
const lsblkColumns = "NAME,KNAME,PATH,SIZE,TYPE,RO,RM,HOTPLUG,TRAN,MODEL,FSTYPE,PTTYPE,LABEL,MOUNTPOINTS"

func parseLsblk(out []byte) ([]blockDev, error) {
	var doc struct {
		Blockdevices []blockDev `json:"blockdevices"`
	}
	if err := json.Unmarshal(out, &doc); err != nil {
		return nil, err
	}
	return doc.Blockdevices, nil
}

const minDataDisk = 1 << 30 // 1 GiB

// blankDisks returns whole disks that carry nothing at all: no partition table, no filesystem
// signature, no partitions, not mounted, writable, at least 1 GiB, and not a removable USB stick.
func blankDisks(devs []blockDev) []Disk {
	var out []Disk
	for _, d := range devs {
		name := d.kernelName()
		if d.Type != "disk" || !ValidDiskName(name) || bool(d.RO) || len(d.Children) > 0 ||
			str(d.Fstype) != "" || str(d.Pttype) != "" || len(d.mountpoints()) > 0 || uint64(d.Size) < minDataDisk ||
			str(d.Tran) == "usb" {
			continue
		}
		out = append(out, Disk{Name: name, SizeBytes: uint64(d.Size), Model: str(d.Model)})
	}
	return out
}

var usbFilesystems = map[string]bool{"vfat": true, "exfat": true, "ntfs": true, "ntfs3": true, "ext4": true, "ext3": true, "ext2": true, "btrfs": true, "xfs": true, "f2fs": true}

// usbDrives returns mountable filesystems on USB disks, excluding the live boot medium and NOVA-DATA.
func usbDrives(devs []blockDev) []USBDrive {
	var out []USBDrive
	add := func(disk, part blockDev) {
		name := part.kernelName()
		if !usbFilesystems[str(part.Fstype)] || str(part.Label) == "NOVA-DATA" || !(ValidPartitionName(name) || ValidDiskName(name)) {
			return
		}
		mps := part.mountpoints()
		for _, m := range mps {
			if strings.HasPrefix(m, "/run/live") || m == "/" || strings.HasPrefix(m, "/var/lib/novaos") {
				return
			}
		}
		drive := USBDrive{ID: name, Name: firstNonEmpty(str(disk.Model), name), Label: str(part.Label), SizeBytes: uint64(part.Size), Mounted: len(mps) > 0}
		if drive.Mounted {
			drive.Mountpoint = mps[0]
		}
		out = append(out, drive)
	}
	for _, d := range devs {
		if d.Type != "disk" || str(d.Tran) != "usb" {
			continue
		}
		if len(d.Children) == 0 {
			add(d, d)
		}
		for _, c := range d.Children {
			add(d, c)
		}
	}
	return out
}
