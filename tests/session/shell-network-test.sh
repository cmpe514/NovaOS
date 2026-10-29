#!/usr/bin/env bash
# Proves the shell browser cannot talk to anything but loopback. Runs Chromium with exactly the flags
# from image/…/usr/lib/novaos/shell for 30 s (headless), records a net-log, and fails if any TCP
# connection went anywhere except 127.0.0.1. (Without the proxy/resolver lock-down, Chromium contacts
# accounts.google.com, android.clients.google.com, update.googleapis.com, … within seconds.)
#   NOVA_CHROMIUM=/path/to/chrome tests/session/shell-network-test.sh   (run as a normal user)
. "$(dirname "$0")/../../tools/lib.sh"
NOVA_STEP=shell-network
need python3 "sudo apt install python3"
chromium_bin="${NOVA_CHROMIUM:-$(command -v chromium || true)}"
[[ -x "$chromium_bin" ]] || die "no Chromium found; set NOVA_CHROMIUM"
[[ $EUID -ne 0 ]] || die "run as a normal user"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
{
  echo '#!/bin/bash'
  sed -n '/^flags=(/,/^)/p' "$ROOT/image/config/includes.chroot/usr/lib/novaos/shell" | grep -v -- '--class\|--user-data-dir\|--ozone-platform\|--enable-wayland-ime'
  printf 'exec timeout %s %q --headless=new "${flags[@]}" --user-data-dir=%q --log-net-log=%q about:blank\n' "${SECONDS_TO_RUN:-30}" "$chromium_bin" "$tmp/profile" "$tmp/net.json"
} > "$tmp/run.sh"
chmod +x "$tmp/run.sh"
log "running the shell browser flags for ${SECONDS_TO_RUN:-30}s with a net-log"
"$tmp/run.sh" >/dev/null 2>&1 || true
python3 - "$tmp/net.json" <<'PY'
import json, sys
text = open(sys.argv[1]).read().rstrip()
if not text.endswith("}"):
    text = text.rstrip(",") + "]}"
log = json.loads(text)
names = {v: k for k, v in log["constants"]["logEventTypes"].items()}
targets = {str(e.get("params", {}).get("address")) for e in log["events"] if names.get(e["type"]) in ("TCP_CONNECT_ATTEMPT", "TCP_CONNECT")}
targets.discard("None")
outside = sorted(t for t in targets if not (t.startswith("127.") or t.startswith("[::1]")))
print(f"TCP connections: {sorted(targets) or 'none'}")
if outside:
    sys.exit(f"FAIL: the shell browser connected to {outside}")
print("PASS: no connection left the machine")
PY
