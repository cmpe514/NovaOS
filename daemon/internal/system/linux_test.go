package system

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const lsblkJSON = `{"blockdevices":[
 {"name":"vda","kname":"vda","path":"/dev/vda","size":21474836480,"type":"disk","ro":false,"rm":false,"hotplug":false,"tran":null,"model":null,"fstype":null,"pttype":"gpt","label":null,"mountpoints":[null],
  "children":[{"name":"vda1","kname":"vda1","path":"/dev/vda1","size":21473787904,"type":"part","ro":false,"rm":false,"hotplug":false,"tran":null,"model":null,"fstype":"ext4","pttype":"gpt","label":"other","mountpoints":[null]}]},
 {"name":"vdb","kname":"vdb","path":"/dev/vdb","size":8589934592,"type":"disk","ro":false,"rm":false,"hotplug":false,"tran":null,"model":"QEMU HARDDISK","fstype":null,"pttype":null,"label":null,"mountpoints":[null]},
 {"name":"vdd","kname":"vdd","path":"/dev/vdd","size":104857600,"type":"disk","ro":false,"rm":false,"hotplug":false,"tran":null,"model":null,"fstype":null,"pttype":null,"label":null,"mountpoints":[null]},
 {"name":"vdc","kname":"vdc","path":"/dev/vdc","size":4294967296,"type":"disk","ro":false,"rm":false,"hotplug":false,"tran":null,"model":null,"fstype":"ext4","pttype":null,"label":"NOVA-DATA","mountpoints":["/var/lib/novaos"]},
 {"name":"sdb","kname":"sdb","path":"/dev/sdb","size":16008609792,"type":"disk","ro":false,"rm":true,"hotplug":true,"tran":"usb","model":"Cruzer Blade","fstype":null,"pttype":"dos","label":null,"mountpoints":[null],
  "children":[{"name":"sdb1","kname":"sdb1","path":"/dev/sdb1","size":16007561216,"type":"part","ro":false,"rm":true,"hotplug":true,"tran":null,"model":null,"fstype":"vfat","pttype":"dos","label":"TRAVEL","mountpoints":["/media/nova/TRAVEL"]}]},
 {"name":"sdc","kname":"sdc","path":"/dev/sdc","size":31914983424,"type":"disk","ro":false,"rm":true,"hotplug":true,"tran":"usb","model":"Boot Stick","fstype":"iso9660","pttype":"dos","label":"NovaOS","mountpoints":[null],
  "children":[{"name":"sdc1","kname":"sdc1","path":"/dev/sdc1","size":1000000000,"type":"part","ro":true,"rm":true,"hotplug":true,"tran":null,"model":null,"fstype":"iso9660","pttype":"dos","label":"NovaOS","mountpoints":["/run/live/medium"]}]},
 {"name":"sr0","kname":"sr0","path":"/dev/sr0","size":1073741824,"type":"rom","ro":false,"rm":true,"hotplug":false,"tran":"sata","model":"QEMU DVD-ROM","fstype":null,"pttype":null,"label":null,"mountpoints":[null]}
]}`

// stubEnv creates a fake sysroot and stub tools that log their argv and print canned output.
type stubEnv struct {
	root, bin, log string
	linux          *Linux
}

func newStubEnv(t *testing.T, outputs map[string]string) *stubEnv {
	t.Helper()
	e := &stubEnv{root: t.TempDir(), bin: t.TempDir()}
	e.log = filepath.Join(t.TempDir(), "calls.log")
	write := func(rel, content string) {
		p := filepath.Join(e.root, rel)
		os.MkdirAll(filepath.Dir(p), 0o755)
		if err := os.WriteFile(p, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("sys/class/power_supply/BAT0/type", "Battery\n")
	write("sys/class/power_supply/BAT0/capacity", "55\n")
	write("sys/class/power_supply/BAT0/status", "Charging\n")
	write("sys/class/power_supply/hidpp_battery_0/type", "Battery\n")
	write("sys/class/power_supply/hidpp_battery_0/scope", "Device\n")
	write("sys/class/power_supply/hidpp_battery_0/capacity", "5\n")
	write("sys/class/power_supply/AC/type", "Mains\n")
	write("sys/class/backlight/intel_backlight/brightness", "500\n")
	write("sys/class/backlight/intel_backlight/max_brightness", "1000\n")
	write("sys/class/backlight/intel_backlight/type", "raw\n")
	write("sys/class/bluetooth/hci0/uevent", "")
	write("usr/lib/systemd/system/novaos-storage-setup@.service", "")
	write("proc/self/mountinfo", "22 1 0:21 / / rw - overlay overlay rw\n97 22 254:32 / /var/lib/novaos rw,nosuid,nodev - ext4 /dev/vdc rw\n")
	os.MkdirAll(filepath.Join(e.root, "dev/disk/by-label"), 0o755)
	os.Symlink("../../vdc", filepath.Join(e.root, "dev/disk/by-label/NOVA-DATA"))

	for _, tool := range allowedTools {
		var cases strings.Builder
		for key, out := range outputs {
			if name, args, _ := strings.Cut(key, " "); name == tool {
				fmt.Fprintf(&cases, "  %q) cat <<'NOVA_EOF'\n%s\nNOVA_EOF\n  ;;\n", args, out)
			}
		}
		script := fmt.Sprintf(`#!/bin/sh
printf '%%s' %q >> %q
for a in "$@"; do printf ' [%%s]' "$a" >> %q; done
printf '\n' >> %q
for a in "$@"; do case "$a" in */.wifi-*) printf 'passwd-file:' >> %q; cat "$a" >> %q;; esac; done
case "$*" in
%s  *) ;;
esac
`, tool, e.log, e.log, e.log, e.log, e.log, cases.String())
		if err := os.WriteFile(filepath.Join(e.bin, tool), []byte(script), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	runtime := t.TempDir()
	e.linux = NewLinux(LinuxOptions{DataMount: "/var/lib/novaos", WebProfile: filepath.Join(t.TempDir(), "web"), RuntimeDir: runtime, SysRoot: e.root}, NewRunnerIn([]string{e.bin}))
	return e
}

func (e *stubEnv) calls(t *testing.T) string {
	b, _ := os.ReadFile(e.log)
	return string(b)
}

var defaultOutputs = map[string]string{
	"nmcli -t -f TYPE,STATE,CONNECTION,DEVICE device status": "wifi:connected:Nova Home:wlp2s0\nethernet:unavailable::enp1s0\nloopback:connected (externally):lo:lo",
	"nmcli radio wifi": "enabled",
	"nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan no":  "*:Nova Home:72:WPA2\n :Café\\:Libre:41:--\n : :30:WPA2",
	"nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan yes": "*:Nova Home:72:WPA2\n :Nova Home:80:WPA2\n :Café\\:Libre:41:--\n :Neighbour:23:WPA3",
	"nmcli -t -f NAME,TYPE connection show":                                 "Nova Home:802-11-wireless\nWired connection 1:802-3-ethernet",
	"bluetoothctl show":                                                     "Controller 00:11:22:33:44:55 (public)\n\tPowered: yes\n\tDiscoverable: no",
	"bluetoothctl devices":                                                  "Device AA:BB:CC:00:11:22 Headphones\nDevice AA:BB:CC:00:11:33 AA-BB-CC-00-11-33",
	"bluetoothctl info AA:BB:CC:00:11:22":                                   "Device AA:BB:CC:00:11:22\n\tAlias: Headphones\n\tPaired: yes\n\tConnected: yes",
	"bluetoothctl info AA:BB:CC:00:11:33":                                   "Device AA:BB:CC:00:11:33\n\tAlias: Mystery\n\tPaired: no\n\tConnected: no",
	"wpctl get-volume @DEFAULT_AUDIO_SINK@":                                 "Volume: 0.40 [MUTED]",
	"timedatectl show -p Timezone -p NTP -p NTPSynchronized":                "Timezone=Asia/Dubai\nNTP=yes\nNTPSynchronized=no",
	"timedatectl list-timezones":                                            "Africa/Abidjan\nAsia/Dubai\nEurope/Lisbon\nUTC",
	"lsblk -J -b -o " + lsblkColumns:                                        lsblkJSON,
}

func TestLinuxStatus(t *testing.T) {
	e := newStubEnv(t, defaultOutputs)
	st, err := e.linux.Status(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if st.Battery == nil || st.Battery.Percent != 55 || !st.Battery.Charging {
		t.Errorf("battery %+v (peripheral batteries must be ignored)", st.Battery)
	}
	if st.Backlight == nil || st.Backlight.Percent != 50 {
		t.Errorf("backlight %+v", st.Backlight)
	}
	if st.Network.State != "connected" || st.Network.Kind != "wifi" || st.Network.SSID != "Nova Home" || st.Network.Signal == nil || *st.Network.Signal != 72 {
		t.Errorf("network %+v", st.Network)
	}
	if st.Wifi == nil || !st.Wifi.Enabled || st.Bluetooth == nil || !st.Bluetooth.Enabled {
		t.Errorf("radios %+v %+v", st.Wifi, st.Bluetooth)
	}
	if st.Audio == nil || st.Audio.Volume != 40 || !st.Audio.Muted {
		t.Errorf("audio %+v", st.Audio)
	}
	if st.Time.Timezone != "Asia/Dubai" || !st.Time.NTPEnabled || st.Time.NTPSynced {
		t.Errorf("time %+v", st.Time)
	}
	if !st.Storage.Persistent || st.Storage.Device != "vdc" || st.Storage.CanSetup {
		t.Errorf("storage %+v", st.Storage)
	}
	if len(st.USB) != 1 || st.USB[0].ID != "sdb1" || st.USB[0].Label != "TRAVEL" || !st.USB[0].Mounted || st.USB[0].Mountpoint != "/media/nova/TRAVEL" {
		t.Errorf("usb %+v (boot medium and NOVA-DATA must be excluded)", st.USB)
	}
}

func TestLinuxNoHardware(t *testing.T) {
	e := newStubEnv(t, map[string]string{"nmcli -t -f TYPE,STATE,CONNECTION,DEVICE device status": "ethernet:connected:Wired:enp1s0"})
	os.RemoveAll(filepath.Join(e.root, "sys/class/power_supply"))
	os.RemoveAll(filepath.Join(e.root, "sys/class/backlight"))
	os.RemoveAll(filepath.Join(e.root, "sys/class/bluetooth"))
	os.Remove(filepath.Join(e.root, "dev/disk/by-label/NOVA-DATA"))
	st, _ := e.linux.Status(context.Background())
	if st.Battery != nil || st.Backlight != nil || st.Bluetooth != nil || st.Wifi != nil || st.Audio != nil {
		t.Errorf("absent hardware must be nil: %+v", st)
	}
	if st.Network.Kind != "ethernet" || st.Network.State != "connected" {
		t.Errorf("network %+v", st.Network)
	}
	if st.Storage.Persistent || !st.Storage.CanSetup {
		t.Errorf("storage %+v", st.Storage)
	}
	if err := e.linux.SetBacklight(context.Background(), 50); err != ErrUnsupported {
		t.Errorf("backlight without hardware: %v", err)
	}
}

func TestLinuxCommands(t *testing.T) {
	e := newStubEnv(t, defaultOutputs)
	ctx := context.Background()
	must := func(err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
	}
	must(e.linux.OpenURL(ctx, "https://example.com/a?b=c"))
	if _, err := e.linux.Power(ctx, "reboot"); err != nil {
		t.Fatal(err)
	}
	if _, err := e.linux.Power(ctx, "halt; rm -rf /"); err == nil {
		t.Fatal("unknown power action accepted")
	}
	must(e.linux.SetBacklight(ctx, 30))
	v, m := 25, false
	must(e.linux.SetAudio(ctx, &v, &m))
	must(e.linux.SetTimezone(ctx, "Europe/Lisbon"))
	if err := e.linux.SetTimezone(ctx, "Mars/Olympus"); err == nil {
		t.Fatal("unknown zone accepted")
	}
	must(e.linux.SetWifi(ctx, false))
	must(e.linux.WifiConnect(ctx, "Neighbour", "correct horse battery"))
	must(e.linux.WifiConnect(ctx, "Nova Home", ""))
	if err := e.linux.BluetoothAction(ctx, "pair", "not-a-mac"); err == nil {
		t.Fatal("bad MAC accepted")
	}
	must(e.linux.BluetoothAction(ctx, "pair", "AA:BB:CC:00:11:33"))
	must(e.linux.USBUnmount(ctx, "sdb1"))
	if err := e.linux.USBMount(ctx, "sdc1"); err == nil {
		t.Fatal("the boot medium must not be mountable through the USB API")
	}
	calls := e.calls(t)
	for _, want := range []string{
		"chromium [--user-data-dir=", "[--class=novaos-web]", "[--] [https://example.com/a?b=c]",
		"systemctl [reboot]",
		"busctl [call] [org.freedesktop.login1] [/org/freedesktop/login1/session/auto] [org.freedesktop.login1.Session] [SetBrightness] [ssu] [backlight] [intel_backlight] [300]",
		"wpctl [set-volume] [@DEFAULT_AUDIO_SINK@] [0.25]", "wpctl [set-mute] [@DEFAULT_AUDIO_SINK@] [0]",
		"timedatectl [set-timezone] [Europe/Lisbon]",
		"nmcli [radio] [wifi] [off]",
		"nmcli [connection] [add] [type] [wifi] [con-name] [Neighbour] [ssid] [Neighbour] [wifi-sec.key-mgmt] [sae]",
		"[passwd-file] [", "passwd-file:802-11-wireless-security.psk:correct horse battery",
		"nmcli [connection] [up] [id] [Nova Home]",
		"bluetoothctl [--timeout] [25] [pair] [AA:BB:CC:00:11:33]", "bluetoothctl [trust] [AA:BB:CC:00:11:33]",
		"udisksctl [unmount] [--no-user-interaction] [-b] [/dev/sdb1]",
	} {
		if !strings.Contains(calls, want) {
			t.Errorf("missing call %q in:\n%s", want, calls)
		}
	}
	for _, line := range strings.Split(calls, "\n") {
		if strings.Contains(line, "correct horse") && !strings.HasPrefix(line, "passwd-file:") {
			t.Errorf("the Wi-Fi password appeared on a command line: %s", line)
		}
		if strings.Contains(line, "halt") || strings.Contains(line, "Mars") || strings.Contains(line, "not-a-mac") || strings.Contains(line, "sdc1") {
			t.Errorf("an invalid argument reached a command: %s", line)
		}
	}
}

func TestLinuxWifiAndBluetoothLists(t *testing.T) {
	e := newStubEnv(t, defaultOutputs)
	nets, err := e.linux.WifiNetworks(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(nets) != 3 || nets[0].SSID != "Nova Home" || !nets[0].Connected || nets[0].Signal != 80 || !nets[0].Known || nets[1].SSID != "Café:Libre" || nets[1].Security != "" {
		t.Fatalf("networks %+v", nets)
	}
	devs, err := e.linux.BluetoothDevices(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(devs) != 2 || !devs[0].Paired || !devs[0].Connected || devs[1].Name != "Mystery" {
		t.Fatalf("devices %+v", devs)
	}
}

func TestBlankDiskDetection(t *testing.T) {
	devs, err := parseLsblk([]byte(lsblkJSON))
	if err != nil {
		t.Fatal(err)
	}
	disks := blankDisks(devs)
	if len(disks) != 1 || disks[0].Name != "vdb" || disks[0].Model != "QEMU HARDDISK" {
		t.Fatalf("only the blank 8 GiB vdb qualifies (not partitioned, formatted, tiny, USB or optical disks): %+v", disks)
	}
	old := []byte(`{"blockdevices":[{"name":"sda","size":"8589934592","type":"disk","ro":"0","rm":"0","fstype":null,"pttype":null,"mountpoint":null}]}`)
	devs, err = parseLsblk(old)
	if err != nil || len(blankDisks(devs)) != 1 {
		t.Fatalf("older lsblk output: %v %+v", err, devs)
	}
}

func TestParsers(t *testing.T) {
	if f := splitTerse(`a\:b:c\\d:`); len(f) != 3 || f[0] != "a:b" || f[1] != `c\d` || f[2] != "" {
		t.Errorf("splitTerse %q", f)
	}
	if a, ok := parseWpctlVolume([]byte("Volume: 1.20")); !ok || a.Volume != 100 || a.Muted {
		t.Errorf("volume %+v", a)
	}
	if _, ok := parseWpctlVolume([]byte("Default sink not found")); ok {
		t.Error("missing sink must fail")
	}
	src, ok := mountSource(writeTemp(t, "1 0 8:1 / /var/lib/nova\\040x rw - ext4 /dev/sda1 rw\n"), "/var/lib/nova x")
	if !ok || src != "/dev/sda1" {
		t.Errorf("mountSource %q %v", src, ok)
	}
}

func writeTemp(t *testing.T, content string) string {
	p := filepath.Join(t.TempDir(), "f")
	os.WriteFile(p, []byte(content), 0o644)
	return p
}
