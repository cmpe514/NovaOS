package system

import (
	"net/url"
	"regexp"
	"strings"
	"unicode"
)

// Every argument that reaches an external command is validated here first. Commands are run with an
// argument vector (never a shell), so these checks are about meaning, not quoting.

var (
	macRE       = regexp.MustCompile(`^[0-9A-F]{2}(:[0-9A-F]{2}){5}$`)
	diskRE      = regexp.MustCompile(`^(sd[a-z]{1,2}|vd[a-z]{1,2}|xvd[a-z]{1,2}|nvme[0-9]{1,2}n[0-9]{1,2}|mmcblk[0-9]{1,2})$`)
	partRE      = regexp.MustCompile(`^(sd[a-z]{1,2}[0-9]{1,3}|vd[a-z]{1,2}[0-9]{1,3}|nvme[0-9]{1,2}n[0-9]{1,2}p[0-9]{1,3}|mmcblk[0-9]{1,2}p[0-9]{1,3})$`)
	zoneRE      = regexp.MustCompile(`^[A-Za-z0-9_+\-]+(/[A-Za-z0-9_+\-]+){0,2}$`)
	hexKeyRE    = regexp.MustCompile(`^[0-9A-Fa-f]{64}$`)
	backlightRE = regexp.MustCompile(`^[A-Za-z0-9_.:\-]{1,64}$`)
)

// ValidateURL accepts absolute http(s) URLs with a host, as the Browser app produces them.
func ValidateURL(raw string) (string, error) {
	if len(raw) == 0 || len(raw) > 2048 {
		return "", userErr("the address is empty or too long")
	}
	for _, r := range raw {
		if unicode.IsSpace(r) || unicode.IsControl(r) {
			return "", userErr("the address contains spaces or control characters")
		}
	}
	u, err := url.Parse(raw)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" || u.User != nil {
		return "", userErr("only http and https web addresses can be opened")
	}
	if strings.HasPrefix(raw, "-") {
		return "", userErr("invalid address")
	}
	return u.String(), nil
}

// ValidMAC checks a Bluetooth address (upper-case, as bluetoothctl prints it).
func ValidMAC(s string) bool { return macRE.MatchString(s) }

// ValidDiskName checks a whole-disk kernel name such as vdb or nvme0n1.
func ValidDiskName(s string) bool { return diskRE.MatchString(s) }

// ValidPartitionName checks a partition kernel name such as sdb1 or mmcblk0p1.
func ValidPartitionName(s string) bool { return partRE.MatchString(s) }

// ValidBlockName accepts either (a USB stick may carry a filesystem without a partition table).
func ValidBlockName(s string) bool { return ValidPartitionName(s) || ValidDiskName(s) }

// ValidZoneSyntax is a cheap pre-check; the zone must also be in the system's zone list.
func ValidZoneSyntax(s string) bool {
	return len(s) <= 64 && zoneRE.MatchString(s) && !strings.Contains(s, "..")
}

// ValidBacklightName checks a /sys/class/backlight entry name.
func ValidBacklightName(s string) bool { return backlightRE.MatchString(s) }

// ValidSSID: 1–32 bytes, no control characters, must not look like an option.
func ValidSSID(s string) error {
	if len(s) == 0 || len(s) > 32 {
		return userErr("network names are 1 to 32 bytes long")
	}
	for _, r := range s {
		if unicode.IsControl(r) {
			return userErr("the network name contains control characters")
		}
	}
	if strings.HasPrefix(s, "-") {
		return userErr("network names starting with '-' are not supported")
	}
	return nil
}

// ValidWifiPassword: empty (open or already-known network), 8–63 printable ASCII, or a 64-digit hex key.
func ValidWifiPassword(s string) error {
	if s == "" || hexKeyRE.MatchString(s) {
		return nil
	}
	if len(s) < 8 || len(s) > 63 {
		return userErr("Wi-Fi passwords are 8 to 63 characters long")
	}
	for _, r := range s {
		if r < 0x20 || r > 0x7e {
			return userErr("the password contains characters Wi-Fi does not allow")
		}
	}
	return nil
}
