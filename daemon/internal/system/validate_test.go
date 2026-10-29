package system

import "testing"

func TestValidateURL(t *testing.T) {
	good := map[string]string{
		"https://example.com/":                                  "https://example.com/",
		"http://neverssl.com/":                                  "http://neverssl.com/",
		"https://www.google.com/search?q=weather%20in%20lisbon": "https://www.google.com/search?q=weather%20in%20lisbon",
	}
	for in, want := range good {
		got, err := ValidateURL(in)
		if err != nil || got != want {
			t.Errorf("%q: got %q, %v", in, got, err)
		}
	}
	for _, bad := range []string{"", "javascript:alert(1)", "file:///etc/passwd", "chrome://settings", "https://", "https://a b.com", "https://user:pw@example.com", "--disable-sandbox", "http://x.com/\n", "data:text/html,hi"} {
		if _, err := ValidateURL(bad); err == nil {
			t.Errorf("%q should be rejected", bad)
		}
	}
}

func TestNames(t *testing.T) {
	for _, ok := range []string{"sdb", "vdb", "nvme0n1", "mmcblk0", "sdaa"} {
		if !ValidDiskName(ok) {
			t.Errorf("disk %q rejected", ok)
		}
	}
	for _, bad := range []string{"", "sda1", "../sda", "sda;rm", "loop0", "sr0", "dm-0", "/dev/sda"} {
		if ValidDiskName(bad) {
			t.Errorf("disk %q accepted", bad)
		}
	}
	for _, ok := range []string{"sdb1", "nvme0n1p2", "mmcblk0p1", "vdc3"} {
		if !ValidPartitionName(ok) {
			t.Errorf("partition %q rejected", ok)
		}
	}
	for _, bad := range []string{"sdb", "sdb1;x", "../sdb1"} {
		if ValidPartitionName(bad) {
			t.Errorf("partition %q accepted", bad)
		}
	}
	if !ValidMAC("AA:BB:CC:00:11:22") || ValidMAC("aa:bb:cc:00:11:22x") || ValidMAC("AA:BB:CC:00:11") {
		t.Error("MAC validation")
	}
	for _, ok := range []string{"UTC", "Europe/Lisbon", "America/Argentina/Buenos_Aires", "Etc/GMT+3"} {
		if !ValidZoneSyntax(ok) {
			t.Errorf("zone %q rejected", ok)
		}
	}
	for _, bad := range []string{"../etc/passwd", "Europe/../../x", "a b", "/UTC", ""} {
		if ValidZoneSyntax(bad) {
			t.Errorf("zone %q accepted", bad)
		}
	}
	if ValidSSID("") == nil || ValidSSID("-x") == nil || ValidSSID("a\nb") == nil || ValidSSID("Home Wi-Fi") != nil {
		t.Error("SSID validation")
	}
	if ValidWifiPassword("short") == nil || ValidWifiPassword("correct horse") != nil || ValidWifiPassword("") != nil || ValidWifiPassword("päss wörd!") == nil {
		t.Error("password validation")
	}
}
