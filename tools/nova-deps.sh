#!/usr/bin/env bash
# Installs the NovaOS build and test toolchain on Ubuntu 24.04 / Debian 13 (also inside WSL2).
# Versions are pinned by distribution release; `make package` records the exact versions in MANIFEST.txt.
set -euo pipefail
pkgs=(make clang lld grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools qemu-system-x86 ovmf python3 python3-pil git)
if command -v sudo >/dev/null && [ "$(id -u)" != 0 ]; then SUDO=sudo; else SUDO=; fi
$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends "${pkgs[@]}"
