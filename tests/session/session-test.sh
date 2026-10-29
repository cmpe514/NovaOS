#!/usr/bin/env bash
# Runs the real NovaOS session — the sway kiosk config, the launch scripts, novad and Chromium — inside a
# headless sway (software rendering) and checks the kiosk rules:
#   1. the shell comes up fullscreen on workspace 1 and reports NOVAOS_READY
#   2. a website window lands on workspace 2 with the Home bar visible
#   3. the Super key brings the shell back
#   4. closing the last browser window returns to the shell automatically
# Needs: sway, grim, wtype, jq, python3, a Chromium binary (NOVA_CHROMIUM or `chromium`), the built UI and
# novad (make ui daemon). Run as a normal user (Chromium refuses to run sandboxed as root).
#   tests/session/session-test.sh            screenshots go to tests/results/session/
. "$(dirname "$0")/../../tools/lib.sh"
NOVA_STEP=session-test
for t in sway grim wtype jq python3; do need "$t" "sudo apt install sway grim wtype jq python3"; done
[[ $EUID -ne 0 ]] || die "run as a normal user, not root (Chromium's sandbox must stay on)"
chromium_bin="${NOVA_CHROMIUM:-$(command -v chromium || true)}"
[[ -x "$chromium_bin" ]] || die "no Chromium found; set NOVA_CHROMIUM=/path/to/chrome"
novad="$ROOT/daemon/bin/novad"; [[ -x "$novad" ]] || die "build novad first: make daemon"
[[ -f "$ROOT/ui/dist/index.html" ]] || die "build the UI first: make ui"
PORT="${PORT:-7191}"
out="$ROOT/tests/results/session"; mkdir -p "$out"
tmp="$(mktemp -d)"; chmod 700 "$tmp"
cleanup() { [[ -n "${sway_pid:-}" ]] && kill "$sway_pid" 2>/dev/null; sleep 0.5; pkill -u "$(id -u)" -f "$tmp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

# Render the image's session files with test paths.
src="$ROOT/image/config/includes.chroot"
mkdir -p "$tmp/root/etc/novaos" "$tmp/root/usr/lib/novaos" "$tmp/bin" "$tmp/run" "$tmp/var/data" "$tmp/home"
cp -r "$src/etc/novaos/sway" "$tmp/root/etc/novaos/"
cp "$src"/usr/lib/novaos/{session,novad-run,shell} "$tmp/root/usr/lib/novaos/"
printf '#!/bin/sh\nexit 0\n' > "$tmp/root/usr/lib/novaos/osk"
printf '#!/bin/sh\nexec %q "$@"\n' "$chromium_bin" > "$tmp/bin/chromium"
ln -s "$novad" "$tmp/bin/novad"
chmod 755 "$tmp"/root/usr/lib/novaos/* "$tmp/bin/chromium"
for f in "$tmp"/root/etc/novaos/sway/config "$tmp"/root/usr/lib/novaos/*; do
  sed -i -e "s|%%PORT%%|$PORT|g" -e "s|%%NAME%%|$(cfg name)|g" -e "s|%%ACCENT_HEX%%|$(node -p 'require(process.argv[1]).accents.mint.slice(1)' "$ROOT/nova.config.json")|g" \
         -e "s|/usr/lib/novaos|$tmp/root/usr/lib/novaos|g" -e "s|/etc/novaos|$tmp/root/etc/novaos|g" \
         -e "s|/usr/bin/novad|$tmp/bin/novad|g" -e "s|/usr/share/novaos/ui|$ROOT/ui/dist|g" \
         -e "s|/var/lib/novaos|$tmp/var|g" "$f"
done

export XDG_RUNTIME_DIR="$tmp/run" HOME="$tmp/home" PATH="$tmp/bin:$PATH"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=1
export NOVAD_ARGS="--system fake --runtime-dir $tmp/run/novaos"
log "starting sway (headless, software rendering)"
sway -c "$tmp/root/etc/novaos/sway/config" >"$tmp/sway.log" 2>&1 &
sway_pid=$!
for _ in $(seq 50); do sock="$(compgen -G "$XDG_RUNTIME_DIR/sway-ipc.*.sock" | head -1 || true)"; [[ -n "$sock" ]] && break; sleep 0.2; done
[[ -n "${sock:-}" ]] || { cat "$tmp/sway.log"; die "sway did not start"; }
export SWAYSOCK="$sock"
WAYLAND_DISPLAY="$(basename "$(compgen -G "$XDG_RUNTIME_DIR/wayland-*" | grep -v "\.lock$" | head -1)")"; export WAYLAND_DISPLAY

fail() { cp "$tmp/sway.log" "$out/sway.log"; ps -u "$(id -u)" -o pid,args > "$out/procs.txt"; swaymsg -t get_bar_config novaos > "$out/bar.json" 2>&1; swaymsg -t get_tree > "$out/tree.json" 2>/dev/null || true; grim "$out/failure.png" 2>/dev/null || true; die "$1 (see tests/results/session/sway.log, tree.json)"; }
# Bound every compositor call: a wedged compositor must fail the test, not hang it.
SWAYMSG_BIN="$(command -v swaymsg)"; GRIM_BIN="$(command -v grim)"; WTYPE_BIN="$(command -v wtype)"
swaymsg() { timeout 5 "$SWAYMSG_BIN" "$@"; }
grim()    { timeout 10 "$GRIM_BIN" "$@"; }
wtype()   { timeout 5 "$WTYPE_BIN" "$@"; }
swaymsg -q output HEADLESS-1 mode 1440x900
window() { swaymsg -r -t get_tree | jq -e --arg re "$1" '[.. | objects | select((.app_id? // "") | test($re))] | length > 0' >/dev/null; }
focused_ws() { swaymsg -r -t get_workspaces | jq -r '.[] | select(.focused) | .num'; }
bar_mode() { swaymsg -r -t get_bar_config novaos | jq -r .mode; }
shell_rect() { swaymsg -r -t get_tree | jq -r '[.. | objects | select((.app_id? // "") | test("^chrome-127\\.0\\.0\\.1__"))][0].rect | "\(.width)x\(.height)"'; }
ws_is()   { test "$(focused_ws)" = "$1"; }
bar_is()  { test "$(bar_mode)" = "$1"; }
rect_is() { test "$(shell_rect)" = "$1"; }
wait_for() { local what="$1" secs="$2"; shift 2; for _ in $(seq $((secs * 5))); do "$@" && return 0; sleep 0.2; done; fail "timed out waiting for $what"; }
not_blank() { python3 - "$1" <<'PY'
import sys,zlib,struct
data=open(sys.argv[1],'rb').read()
assert data[:8]==b'\x89PNG\r\n\x1a\n'
pos,idat,w,h=8,b'',0,0
while pos<len(data):
    n,=struct.unpack('>I',data[pos:pos+4]);t=data[pos+4:pos+8];c=data[pos+8:pos+8+n];pos+=12+n
    if t==b'IHDR':w,h=struct.unpack('>II',c[:8]);bpp=4 if c[9]==6 else 3
    if t==b'IDAT':idat+=c
raw=zlib.decompress(idat);colors=set();stride=w*bpp
for y in range(0,h,7):
    row=raw[y*(stride+1)+1:(y+1)*(stride+1)]  # unfiltered bytes are approximate, enough to spot a blank frame
    for x in range(0,w,11):colors.add(row[x*bpp:x*bpp+3])
print(len(colors));sys.exit(0 if len(colors)>=24 else 1)
PY
}

log "waiting for the shell window"
wait_for "the shell window" 90 window "^chrome-127\\.0\\.0\\.1__"
wait_for "NOVAOS_READY from novad" 60 grep -q NOVAOS_READY "$tmp/sway.log"
[[ "$(focused_ws)" == 1 ]] || fail "the shell is not on workspace 1"
wait_for "the bar to hide on the shell" 5 bar_is invisible
wait_for "the shell to fill the screen" 5 rect_is 1440x900
sleep 1; grim "$out/1-home.png"; not_blank "$out/1-home.png" >/dev/null || fail "the home screen screenshot is blank"
log "PASS shell fills the screen on workspace 1 (bar hidden), NOVAOS_READY seen"
if [[ -n "${HOLD_SHELL_SECONDS:-}" ]]; then log "holding on the shell for ${HOLD_SHELL_SECONDS}s (HOLD_SHELL_SECONDS)"; sleep "$HOLD_SHELL_SECONDS"; log "hold finished at $(date -u +%H:%M:%S)"; fi

log "opening a website window (as the Browser app does)"
chromium --class=novaos-web --user-data-dir="$tmp/web" --ozone-platform=wayland --no-first-run --no-default-browser-check \
  "http://127.0.0.1:$PORT/healthz" >/dev/null 2>&1 &
wait_for "the browser window" 60 window "^novaos-web$"
wait_for "the browser workspace" 10 ws_is 2
wait_for "the Home bar to dock" 5 bar_is dock
sleep 1; grim "$out/2-browser.png"
log "PASS browser on workspace 2 with the Home bar"

log "pressing Super"
wtype -k Super_L
wait_for "Super to return home" 5 ws_is 1
log "PASS Super returns to the shell"

swaymsg -q workspace number 2
log "closing the browser window"
swaymsg -q '[app_id="^novaos-web$"]' kill
wait_for "automatic return home" 10 ws_is 1
grim "$out/3-after-close.png"
log "PASS closing the browser returns to the shell"
cp "$tmp/sway.log" "$out/sway.log"
log "all session checks passed; screenshots in tests/results/session/"
