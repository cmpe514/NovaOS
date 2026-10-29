#!/usr/bin/env bash
# Boots the newest ISO in QEMU with a window, sound, networking, a USB tablet (exact mouse) and a
# persistent 8 GiB data disk (out/data-<arch>.qcow2, blank on first run: set it up from Settings).
#   tools/run-qemu.sh bios|uefi        ISO=path  MEM=3072  CPUS=2  DATA_DISK=path  NO_DATA_DISK=1
# Acceleration: KVM on Linux, HVF on macOS, WHPX on Windows; falls back to (slow) emulation.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=run
mode="${1:-bios}"; ARCH="${ARCH:-amd64}"
qarch="$(arch_qemu "$ARCH")"
qemu="qemu-system-$qarch"
need "$qemu" "Linux: sudo apt install qemu-system-x86 qemu-utils ovmf (arm64: qemu-system-arm qemu-efi-aarch64) | macOS: brew install qemu | Windows: winget install SoftwareFreedomConservancy.QEMU"
need qemu-img "install qemu-utils (comes with QEMU on macOS/Windows)"
id="$(cfg id)"
iso="${ISO:-$(ls -t "$ROOT"/out/"$id"-*-"$ARCH"*.iso 2>/dev/null | head -1 || true)}"
[[ -f "$iso" ]] || die "no ISO found in out/ for $ARCH. Build one first: make iso ARCH=$ARCH"
[[ "$ARCH" == arm64 && "$mode" == bios ]] && mode=uefi   # arm64 has no BIOS

os="$(uname -s)"
accel=(-accel tcg); cpu=(-cpu max)
case "$os" in
  Linux)
    if [[ -r /dev/kvm && -w /dev/kvm && "$qarch" == "$(uname -m)" ]]; then accel=(-accel kvm); cpu=(-cpu host)
    else warn "KVM is not available (no /dev/kvm or no permission: sudo usermod -aG kvm \$USER, then log in again); emulation is slow"; fi ;;
  Darwin)
    host_arch="$(uname -m)"; [[ "$host_arch" == arm64 ]] && host_arch=aarch64
    if [[ "$qarch" == "$host_arch" ]]; then accel=(-accel hvf); cpu=(-cpu host)
    else warn "this Mac cannot accelerate $qarch guests; on Apple Silicon build and run ARCH=arm64"; fi ;;
  MINGW*|MSYS*|CYGWIN*) accel=(-accel whpx -accel tcg); cpu=(-cpu max) ;;
esac

case "$os" in
  Darwin) audio=(-audiodev coreaudio,id=snd) ;;
  MINGW*|MSYS*|CYGWIN*) audio=(-audiodev dsound,id=snd) ;;
  *) if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then audio=(-audiodev pa,id=snd); else audio=(-audiodev alsa,id=snd); fi ;;
esac

machine=(); firmware=(); display=(-device virtio-vga)
if [[ "$qarch" == x86_64 ]]; then machine=(-machine q35); else machine=(-machine virt); display=(-device virtio-gpu-pci -device ramfb); fi
if [[ "$mode" == uefi ]]; then
  code=""; vars_template=""
  for c in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd /usr/share/edk2/x64/OVMF_CODE.4m.fd \
           /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/local/share/qemu/edk2-x86_64-code.fd /usr/share/qemu/edk2-x86_64-code.fd; do
    [[ "$qarch" == x86_64 && -f "$c" ]] && { code="$c"; break; }
  done
  for c in /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd /opt/homebrew/share/qemu/edk2-aarch64-code.fd /usr/local/share/qemu/edk2-aarch64-code.fd /usr/share/qemu/edk2-aarch64-code.fd; do
    [[ "$qarch" == aarch64 && -z "$code" && -f "$c" ]] && { code="$c"; break; }
  done
  [[ -n "$code" ]] || die "no UEFI firmware found. Linux: sudo apt install ovmf (arm64: qemu-efi-aarch64); macOS: included with brew's qemu"
  vars="$ROOT/out/uefi-vars-$ARCH.fd"
  if [[ ! -f "$vars" ]]; then
    for v in "${code/CODE/VARS}" "${code/-code/-vars}" "${code/code/vars}"; do [[ -f "$v" && "$v" != "$code" ]] && { vars_template="$v"; break; }; done
    if [[ -n "$vars_template" ]]; then cp "$vars_template" "$vars"; else qemu-img create -f raw "$vars" 64M >/dev/null; fi
  fi
  firmware=(-drive "if=pflash,format=raw,readonly=on,file=$code" -drive "if=pflash,format=raw,file=$vars")
fi

data=()
if [[ "${NO_DATA_DISK:-0}" != 1 ]]; then
  disk="${DATA_DISK:-$ROOT/out/data-$ARCH.qcow2}"
  [[ -f "$disk" ]] || { qemu-img create -f qcow2 "$disk" 8G >/dev/null; log "created a blank data disk $disk (set it up from Settings → Storage)"; }
  data=(-drive "file=$disk,if=virtio,format=qcow2,cache=writeback")
fi

log "booting $(basename "$iso") ($mode, ${accel[*]}) — close the window or press Ctrl+C here to stop"
log "serial console log: out/serial-$ARCH.log"
exec "$qemu" "${machine[@]}" "${accel[@]}" "${cpu[@]}" -smp "${CPUS:-2}" -m "${MEM:-3072}" \
  "${firmware[@]}" \
  -drive "file=$iso,media=cdrom,readonly=on,if=none,id=cd" -device virtio-scsi-pci -device scsi-cd,drive=cd,bootindex=0 \
  "${data[@]}" \
  "${display[@]}" -display "${DISPLAY_BACKEND:-default}" \
  "${audio[@]}" -device intel-hda -device hda-duplex,audiodev=snd \
  -nic user,model=virtio-net-pci \
  -device qemu-xhci -device usb-tablet -device usb-kbd \
  -serial "file:$ROOT/out/serial-$ARCH.log" \
  -name "$(cfg name)" ${QEMU_EXTRA:-}
