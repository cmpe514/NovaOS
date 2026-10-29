#!/usr/bin/env bash
# Builds the bootable hybrid ISO (BIOS + UEFI on amd64, UEFI on arm64) with Debian live-build.
# Run through `make iso` (inside the privileged builder container) or with NATIVE=1 as root on Debian 13.
#   ARCH=amd64|arm64   DEBUG=0|1   NOVA_MIRROR=http://deb.debian.org/debian/   VERBOSE=1
# Output: out/<id>-<version>-<arch>[-debug].iso (+ .sha256, build log, package manifest)
. "$(dirname "$0")/lib.sh"
NOVA_STEP=iso
ARCH="${ARCH:-amd64}"
DEBUG="${DEBUG:-0}"
DRY_RUN="${NOVA_DRY_RUN:-0}"   # 1: render the live-build tree and stop (no root, no live-build needed)
if [[ "$DRY_RUN" != 1 ]]; then
  [[ $EUID -eq 0 ]] || die "live-build must run as root. Use 'make iso' (container) or 'sudo make iso NATIVE=1' on Debian 13."
  need lb "sudo apt install live-build"
  need debootstrap "sudo apt install debootstrap"
  need xorriso "sudo apt install xorriso"
fi
need python3 "sudo apt install python3"
need rsync "sudo apt install rsync"
[[ -f "$ROOT/ui/dist/index.html" ]] || die "the UI is not built: run 'make ui' first"
[[ -x "$ROOT/daemon/bin/novad-$ARCH" ]] || die "novad for $ARCH is not built: run 'make daemon ARCH=$ARCH' first"
if [[ -f /etc/debian_version ]]; then :; else warn "live-build is only supported on Debian; use the builder container (make iso)"; fi

name="$(cfg name)"; id="$(cfg id)"; version="$(cfg version)"; user="$(cfg user)"
port="$(cfg daemon_port)"; host="$(cfg hostname)"; suite="$(cfg debian_suite)"
accent="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["accents"]["mint"])' "$ROOT/nova.config.json")"
mirror="${NOVA_MIRROR:-http://deb.debian.org/debian/}"
mirror_security="${NOVA_MIRROR_SECURITY:-http://security.debian.org/debian-security/}"

if [[ -d /build && -f /.novaos-builder ]]; then work="/build/live-$ARCH"; else work="$ROOT/build/live-$ARCH"; fi
[[ "$DRY_RUN" == 1 ]] && work="$ROOT/build/dry-run-$ARCH"
suffix=""; [[ "$DEBUG" == 1 ]] && suffix="-debug"
iso_name="$id-$version-$ARCH$suffix.iso"
mkdir -p "$ROOT/out"

case "$ARCH" in
  amd64) flavour=amd64; bootloaders="grub-pc grub-efi"; serial=ttyS0 ;;
  arm64) flavour=arm64; bootloaders="grub-efi"; serial=ttyAMA0 ;;
  *) die "unsupported ARCH '$ARCH' (amd64 or arm64)" ;;
esac
bootloaders="${NOVA_BOOTLOADERS:-$bootloaders}"
extra_lb=""
host_arch="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
[[ "${NOVA_DRY_RUN:-0}" == 1 ]] && host_arch="$ARCH"
if [[ "$ARCH" != "$host_arch" ]]; then
  qemu_static="/usr/bin/qemu-$( [[ $ARCH == arm64 ]] && echo aarch64 || echo x86_64 )-static"
  [[ -x "$qemu_static" ]] || die "cross-building $ARCH needs $qemu_static (sudo apt install qemu-user-static binfmt-support)"
  extra_lb="--bootstrap-qemu-arch $ARCH --bootstrap-qemu-static $qemu_static"
fi

quiet="quiet splash loglevel=3 rd.udev.log_level=3 udev.log_level=3 systemd.show_status=false rd.systemd.show_status=false vt.global_cursor_default=0 plymouth.ignore-serial-consoles"
[[ "$DEBUG" == 1 ]] && quiet="splash systemd.show_status=auto plymouth.ignore-serial-consoles"
consoles="console=$serial,115200n8 console=tty1"
append="boot=live $quiet $consoles"
append_safe="boot=live novaos.safe=1 novaos.renderer=pixman nomodeset $consoles"
append_recovery="boot=live novaos.nodata systemd.unit=novaos-recovery.target $consoles"

debug_password=""
if [[ "$DEBUG" == 1 ]]; then
  debug_password="$(head -c 12 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 12)"
fi
iso_volume="$(echo "$name" | tr -cd 'A-Za-z0-9_-' | cut -c1-32)"

log "preparing $work"
if [[ -d "$work/config" && "$DRY_RUN" != 1 ]]; then (cd "$work" && lb clean noauto >/dev/null 2>&1 || true); fi
mkdir -p "$work"
rm -rf "$work/config" "$work/auto"
rsync -a "$ROOT/image/auto/" "$work/auto/"
rsync -a --exclude '*.disabled' "$ROOT/image/config/" "$work/config/"
if [[ "$DEBUG" == 1 ]]; then
  cp "$ROOT/image/config/package-lists/debug.list.chroot.disabled" "$work/config/package-lists/debug.list.chroot"
  cp "$ROOT/image/config/hooks/normal/0600-novaos-debug.hook.chroot.disabled" "$work/config/hooks/normal/0600-novaos-debug.hook.chroot"
  if [[ -s "$ROOT/out/debug_authorized_keys" ]]; then
    install -D -m 0644 "$ROOT/out/debug_authorized_keys" "$work/config/includes.chroot/etc/novaos/debug-authorized_keys"
    log "debug build: SSH key login from out/debug_authorized_keys"
  fi
fi

inc="$work/config/includes.chroot"
log "adding novad, the shell UI and the licence files"
install -D -m 0755 "$ROOT/daemon/bin/novad-$ARCH" "$inc/usr/bin/novad"
mkdir -p "$inc/usr/share/novaos"
rsync -a --delete "$ROOT/ui/dist/" "$inc/usr/share/novaos/ui/"
for f in README.md THIRD_PARTY_LICENSES CREDITS docs/SECURITY.md docs/ARCHITECTURE.md; do
  [[ -f "$ROOT/$f" ]] && install -D -m 0644 "$ROOT/$f" "$inc/usr/share/doc/novaos/$(basename "$f")"
done

splash_dir="$ROOT/image/config/includes.chroot/usr/share/plymouth/themes/novaos"
want_splash="$(printf '%s\n%s\n%s\n' "$name" "$(cfg tagline)" "$accent")"
if [[ "$(cat "$splash_dir/.rendered-for" 2>/dev/null)" != "$want_splash" ]]; then
  if [[ -d "$ROOT/tests/node_modules/playwright" ]] && node "$ROOT/tools/render-splash.mjs"; then
    rsync -a "$splash_dir/" "$inc/usr/share/plymouth/themes/novaos/"
  else
    warn "the boot splash still shows the old name; run 'cd tests && npm ci && node ../tools/render-splash.mjs'"
  fi
fi

tokens="$work/tokens.json"
python3 - "$tokens" <<PY
import json,sys
json.dump({
  "NAME": """$name""", "ID": "$id", "VERSION": "$version", "TAGLINE": """$(cfg tagline)""",
  "USER": "$user", "PORT": "$port", "HOSTNAME": "$host", "SUITE": "$suite", "ARCH": "$ARCH",
  "ACCENT": "$accent", "ACCENT_HEX": "${accent#\#}",
  "MIRROR": "$mirror", "MIRROR_SECURITY": "$mirror_security",
  "BOOTLOADERS": "$bootloaders", "FLAVOUR": "$flavour", "ISO_VOLUME": "$iso_volume",
  "APPEND": "$append", "APPEND_SAFE": "$append_safe", "APPEND_RECOVERY": "$append_recovery",
  "TIMEOUT": "$([[ $DEBUG == 1 ]] && echo 5 || echo 1)",
  "TIMEOUT_STYLE": "$([[ $DEBUG == 1 ]] && echo menu || echo hidden)",
  "EXTRA_ENTRIES": "",
  "EXTRA_LB_OPTIONS": "$extra_lb",
  "DEBUG": "$DEBUG", "DEVTOOLS": "$([[ $DEBUG == 1 ]] && echo 1 || echo 2)",
  "DEBUG_PASSWORD": "$debug_password",
}, open(sys.argv[1], "w"), indent=1)
PY
python3 "$ROOT/tools/render-templates.py" "$tokens" "$work/auto" "$work/config"
rm -f "$tokens"
if [[ "$DRY_RUN" == 1 ]]; then log "dry run: rendered live-build tree in $work"; exit 0; fi

log "lb config ($ARCH, $suite, bootloaders: $bootloaders, mirror: $mirror)"
cd "$work"
./auto/config
log "lb build — the first build downloads ~1 GB of packages and takes 15–40 minutes"
start=$(date +%s)
if ! ./auto/build; then
  cp build.log "$ROOT/out/build-$ARCH$suffix.log" 2>/dev/null || true
  die "lb build failed. The full log is out/build-$ARCH$suffix.log; the cause is usually in the last 40 lines (tail -n 40 out/build-$ARCH$suffix.log)."
fi
iso="$(ls -t live-image-"$ARCH".hybrid.iso 2>/dev/null | head -1 || true)"
[[ -f "$iso" ]] || die "lb build finished but no ISO was produced (see out/build-$ARCH$suffix.log)"
cp "$iso" "$ROOT/out/$iso_name"
cp build.log "$ROOT/out/build-$ARCH$suffix.log"
[[ -f chroot.packages.live ]] && cp chroot.packages.live "$ROOT/out/$iso_name.packages"
(cd "$ROOT/out" && sha256sum "$iso_name" > "$iso_name.sha256")
size=$(stat -c %s "$ROOT/out/$iso_name")
log "built out/$iso_name ($((size / 1048576)) MiB) in $(( ($(date +%s) - start) / 60 )) min"
(( size <= 1610612736 )) || warn "the ISO is larger than the 1.5 GB target"
if [[ "$DEBUG" == 1 ]]; then
  if [[ -n "$debug_password" && ! -s "$ROOT/out/debug_authorized_keys" ]]; then
    echo "$debug_password" > "$ROOT/out/$iso_name.dev-password"; chmod 600 "$ROOT/out/$iso_name.dev-password"
    log "debug build: user 'dev', password in out/$iso_name.dev-password (tty2: Ctrl+Alt+F2, or SSH)"
  fi
fi
log "next: make run   (or: make run-uefi, make smoke, make usb DEV=/dev/sdX)"
