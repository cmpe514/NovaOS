#!/usr/bin/env bash
# Checks everything the NovaOS workflow needs and prints the exact command to fix each problem.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=doctor
os="$(uname -s)"; ok=0; bad=0
pass() { printf '  \033[32m✓\033[0m %s\n' "$1"; ok=$((ok+1)); }
fail() { printf '  \033[31m✗\033[0m %s\n      fix: %s\n' "$1" "$2"; bad=$((bad+1)); }
note() { printf '  \033[33m!\033[0m %s\n' "$1"; }
has() { command -v "$1" >/dev/null 2>&1; }
pm() { case "$os" in Darwin) echo "brew install $2";; MINGW*|MSYS*|CYGWIN*) echo "winget install $3";; *) echo "sudo apt install $1";; esac; }

echo "System: $os $(uname -m)"
if grep -qi microsoft /proc/version 2>/dev/null; then echo "        (WSL2 detected: run everything inside WSL; Docker Desktop must have WSL integration enabled)"; fi

echo; echo "Building (container workflow, recommended)"
engine=""; for e in podman docker; do has "$e" && { engine="$e"; break; }; done
if [[ -z "$engine" ]]; then
  fail "Docker or Podman" "$(pm 'docker.io' '--cask docker' 'Docker.DockerDesktop')  (then start it)"
elif ! "$engine" info >/dev/null 2>&1; then
  fail "$engine is installed but not running or not accessible" "start Docker Desktop, or: sudo systemctl enable --now docker && sudo usermod -aG docker \$USER (log out and in)"
else
  pass "$engine is running"
  if "$engine" run --rm --privileged docker.io/library/debian:trixie true >/dev/null 2>&1; then pass "privileged containers work (needed by live-build)"
  else fail "privileged containers do not work" "rootless Podman: run with sudo; Docker: make sure you are not in a restricted context (CI runners need privileged mode)"; fi
fi
free_kb="$(df -Pk "$ROOT" | awk 'NR==2{print $4}')"
if (( free_kb > 20*1024*1024 )); then pass "disk space: $((free_kb/1024/1024)) GB free"; else fail "only $((free_kb/1024/1024)) GB free; the build needs about 20 GB" "free some space (docker system prune; make distclean)"; fi
has git && pass "git" || fail "git" "$(pm git git Git.Git)"
has make && pass "make" || fail "make" "$(pm make make GnuWin32.Make)"

echo; echo "Native build (NATIVE=1, Debian 13 only; optional)"
for t in lb:live-build debootstrap:debootstrap xorriso:xorriso go:golang-go node:nodejs npm:npm rsync:rsync python3:python3; do
  has "${t%%:*}" && pass "${t%%:*}" || note "${t%%:*} missing (only for NATIVE=1): sudo apt install ${t#*:}"
done
if has go; then v="$(go env GOVERSION | sed 's/go//')"; [[ "$(printf '%s\n1.22\n' "$v" | sort -V | head -1)" == 1.22 ]] && pass "Go $v" || note "Go $v is older than 1.22"; fi
if has node; then v="$(node -v | tr -d v)"; (( ${v%%.*} >= 20 )) && pass "Node.js $v" || note "Node.js $v is older than 20"; fi

echo; echo "Running in a VM (make run / run-uefi / smoke)"
case "$(uname -m)" in x86_64|amd64) q=qemu-system-x86_64;; arm64|aarch64) q=qemu-system-aarch64;; *) q=qemu-system-x86_64;; esac
has "$q" && pass "$q" || fail "$q" "$(pm 'qemu-system-x86 qemu-utils ovmf' qemu SoftwareFreedomConservancy.QEMU)"
has qemu-img && pass "qemu-img" || fail "qemu-img" "$(pm qemu-utils qemu SoftwareFreedomConservancy.QEMU)"
case "$os" in
  Linux)
    if [[ -e /dev/kvm ]]; then
      if [[ -r /dev/kvm && -w /dev/kvm ]]; then pass "KVM acceleration"; else fail "KVM exists but you cannot use it" "sudo usermod -aG kvm \$USER  (then log out and in)"; fi
    else fail "no KVM (/dev/kvm missing): VMs will be very slow" "enable virtualisation (VT-x/AMD-V) in the firmware settings; in WSL2 or a cloud VM enable nested virtualisation"; fi
    { compgen -G "/usr/share/OVMF/OVMF_CODE*.fd" || compgen -G "/usr/share/edk2/ovmf/OVMF_CODE*.fd" || compgen -G "/usr/share/edk2/x64/OVMF_CODE*.fd"; } >/dev/null && pass "UEFI firmware (OVMF)" || fail "UEFI firmware (OVMF) for make run-uefi" "sudo apt install ovmf" ;;
  Darwin)
    [[ "$(sysctl -n kern.hv_support 2>/dev/null)" == 1 ]] && pass "Hypervisor.framework (HVF)" || fail "HVF unavailable" "use a Mac with virtualisation support"
    [[ "$(uname -m)" == arm64 ]] && note "Apple Silicon: build and run the arm64 image (make iso ARCH=arm64; make run-uefi ARCH=arm64) for full speed; amd64 runs emulated." ;;
  MINGW*|MSYS*|CYGWIN*) note "Windows: QEMU uses WHPX acceleration; enable 'Windows Hypervisor Platform' in Windows Features." ;;
esac

echo
if (( bad )); then echo "$bad problem(s) found — run the fix commands above, then 'make doctor' again."; exit 1; fi
echo "All set ($ok checks passed). Next: make iso && make run"
