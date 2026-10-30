#!/usr/bin/env bash
# Checks that everything `make image` and `make test` need is installed, and says how to fix what is missing.
set -u
ok=1
need() {  # need <command> <package>
    if command -v "$1" >/dev/null 2>&1; then printf '  ok       %-22s %s\n' "$1" "$("$1" --version 2>/dev/null | head -1)"
    else printf '  MISSING  %-22s (apt package: %s)\n' "$1" "$2"; ok=0; fi
}
echo "NovaOS build doctor"
need clang clang
need ld.lld lld
need make make
need grub-mkrescue grub-common
need xorriso xorriso
need mformat mtools
need qemu-system-x86_64 qemu-system-x86
need qemu-system-aarch64 qemu-system-arm
need python3 python3
[ -d /usr/lib/grub/i386-pc ] && echo "  ok       GRUB BIOS modules" || { echo "  MISSING  GRUB BIOS modules (grub-pc-bin)"; ok=0; }
[ -d /usr/lib/grub/x86_64-efi ] && echo "  ok       GRUB UEFI modules" || { echo "  MISSING  GRUB UEFI modules (grub-efi-amd64-bin)"; ok=0; }
[ -f /usr/share/ovmf/OVMF.fd ] && echo "  ok       OVMF (UEFI firmware for tests)" || echo "  optional OVMF missing: UEFI test is skipped as FAIL (apt package: ovmf)"
python3 -c 'import PIL' 2>/dev/null && echo "  ok       python3-pil" || { echo "  MISSING  python3 PIL (python3-pil), needed by make test"; ok=0; }
[ -w /dev/kvm ] && echo "  ok       KVM acceleration" || echo "  note     no /dev/kvm: QEMU runs without acceleration (slower, still works)"
if [ $ok = 1 ]; then echo "All required tools found."; else echo "Install the missing tools with: tools/nova-deps.sh"; exit 1; fi
