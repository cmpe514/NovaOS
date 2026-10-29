#!/usr/bin/env bash
# Headless boot test. For each firmware mode (BIOS and UEFI on amd64), boots the newest ISO with no
# display, waits for the NOVAOS_READY line on the serial console (sent once the home screen has
# rendered), grabs a screenshot through QMP and fails if it is blank.
# With PERSIST=1 (default) the UEFI run uses a pre-formatted NOVA-DATA disk and boots twice, shutting
# down cleanly in between, and requires boots=2 on the second boot: mount, write and unmount all work.
#   tools/smoke-qemu.sh        MODES="bios uefi"  TIMEOUT=seconds  ISO=path  PERSIST=0|1  MIN_COLORS=24
. "$(dirname "$0")/lib.sh"
NOVA_STEP=smoke
ARCH="${ARCH:-amd64}"; qarch="$(arch_qemu "$ARCH")"; qemu="qemu-system-$qarch"
need "$qemu" "sudo apt install qemu-system-x86 ovmf (or use the builder: make smoke)"
need qemu-img "sudo apt install qemu-utils"
need python3 "sudo apt install python3"
id="$(cfg id)"
iso="${ISO:-$(ls -t "$ROOT"/out/"$id"-*-"$ARCH"*.iso 2>/dev/null | head -1 || true)}"
[[ -f "$iso" ]] || die "no ISO in out/ (make iso)"
default_modes="bios uefi"; [[ "$ARCH" == arm64 ]] && default_modes="uefi"
MODES="${MODES:-$default_modes}"
accel=(-accel tcg); cpu=(-cpu max); timeout="${TIMEOUT:-}"
if [[ -r /dev/kvm && -w /dev/kvm && "$qarch" == "$(uname -m)" ]]; then accel=(-accel kvm); cpu=(-cpu host); timeout="${timeout:-180}"
else warn "no KVM: emulated boot, allowing up to 20 minutes per boot"; timeout="${timeout:-1200}"; fi
out="$ROOT/out/smoke"; mkdir -p "$out"
tmp="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$tmp"' EXIT

firmware_for() {
  [[ "$1" == uefi ]] || return 0
  local code vars
  for code in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/qemu/edk2-x86_64-code.fd \
              /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd; do
    [[ -f "$code" ]] && break
  done
  [[ -f "$code" ]] || die "UEFI firmware not found (sudo apt install ovmf)"
  vars="$tmp/vars-$2.fd"
  if [[ -f "${code/CODE/VARS}" && "${code/CODE/VARS}" != "$code" ]]; then cp "${code/CODE/VARS}" "$vars"; else qemu-img create -f raw "$vars" 64M >/dev/null; fi
  echo "-drive if=pflash,format=raw,readonly=on,file=$code -drive if=pflash,format=raw,file=$vars"
}

# boot NAME MODE [DISK] — returns once NOVAOS_READY was seen (prints the line) or fails.
boot() {
  local name="$1" mode="$2" disk="${3:-}" serial="$out/$1.serial.log" qmp="$tmp/$1.qmp" machine=(-machine q35) display=(-device virtio-vga)
  [[ "$qarch" == aarch64 ]] && { machine=(-machine virt); display=(-device virtio-gpu-pci); }
  local diskargs=(); [[ -n "$disk" ]] && diskargs=(-drive "file=$disk,if=virtio,format=raw")
  : > "$serial"
  # shellcheck disable=SC2046
  "$qemu" "${machine[@]}" "${accel[@]}" "${cpu[@]}" -smp 2 -m 3072 $(firmware_for "$mode" "$name") \
    -drive "file=$iso,media=cdrom,readonly=on,if=none,id=cd" -device virtio-scsi-pci -device scsi-cd,drive=cd,bootindex=0 \
    "${diskargs[@]}" "${display[@]}" -display none -audiodev none,id=snd -device intel-hda -device hda-duplex,audiodev=snd \
    -nic user,model=virtio-net-pci -device qemu-xhci -device usb-tablet \
    -serial "file:$serial" -qmp "unix:$qmp,server=on,wait=off" -no-reboot &
  local pid=$! start; start=$(date +%s)
  log "$name: booting ($mode, ${accel[*]}), waiting up to ${timeout}s for NOVAOS_READY"
  while ! grep -aq NOVAOS_READY "$serial"; do
    kill -0 "$pid" 2>/dev/null || die "$name: QEMU exited before the home screen came up (serial log: out/smoke/$name.serial.log)"
    (( $(date +%s) - start < timeout )) || { python3 "$ROOT/tools/qmp.py" "$qmp" screendump "{\"filename\":\"$out/$name.timeout.ppm\"}" >/dev/null 2>&1 || true
      kill "$pid"; die "$name: no NOVAOS_READY after ${timeout}s (serial log: out/smoke/$name.serial.log)"; }
    sleep 2
  done
  local line rest; line="$(grep -a -o 'NOVAOS_READY[^[:cntrl:]]*' "$serial" | head -1)"
  rest="${line#NOVAOS_READY}"; line="NOVAOS_READY${rest%%NOVAOS_READY*}"   # serial output can repeat on one line
  log "$name: $line (wall clock $(( $(date +%s) - start ))s)"
  sleep 3   # let the first frame settle
  python3 "$ROOT/tools/qmp.py" "$qmp" screendump "{\"filename\":\"$tmp/$name.ppm\"}" >/dev/null || die "$name: screendump failed"
  python3 "$ROOT/tools/screen-check.py" "$tmp/$name.ppm" "$out/$name.png" "${MIN_COLORS:-24}" || die "$name: the screen is blank (out/smoke/$name.png)"
  log "$name: screenshot out/smoke/$name.png is not blank"
  LAST_LINE="$line"
  log "$name: shutting down (ACPI power button → clean unmount)"
  python3 "$ROOT/tools/qmp.py" "$qmp" system_powerdown >/dev/null || true
  for _ in $(seq 1 90); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
  if kill -0 "$pid" 2>/dev/null; then kill "$pid"; warn "$name: did not power off within 90 s"; return 1; fi
  wait "$pid" 2>/dev/null || true
}

for mode in $MODES; do boot "$mode" "$mode"; done

if [[ "${PERSIST:-1}" == 1 ]]; then
  need mkfs.ext4 "sudo apt install e2fsprogs (Linux only; run 'make smoke' in the builder on macOS/Windows)"
  disk="$tmp/data.img"; truncate -s 2G "$disk"; mkfs.ext4 -q -F -L NOVA-DATA "$disk"
  mode="$(echo "$MODES" | awk '{print $NF}')"
  boot persist-1 "$mode" "$disk" || die "persist-1: unclean shutdown"
  [[ "$LAST_LINE" == *persistent=yes* && "$LAST_LINE" == *write=ok* ]] || die "persist-1: the NOVA-DATA disk was not used ($LAST_LINE)"
  boot persist-2 "$mode" "$disk"
  [[ "$LAST_LINE" == *boots=2* ]] || die "persist-2: data did not survive the reboot ($LAST_LINE)"
  log "persistence: the data disk was mounted, written and still had the data after a clean reboot"
fi
log "smoke test passed ($MODES); screenshots in out/smoke/"
