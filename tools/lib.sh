#!/usr/bin/env bash
# Shared helpers for NovaOS scripts. Source it: . "$(dirname "$0")/lib.sh"
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ROOT

log()  { printf '\033[1;36m[%s]\033[0m %s\n' "${NOVA_STEP:-nova}" "$*" >&2; }
warn() { printf '\033[1;33m[%s] WARNING:\033[0m %s\n' "${NOVA_STEP:-nova}" "$*" >&2; }
die()  { printf '\033[1;31m[%s] ERROR:\033[0m %s\n' "${NOVA_STEP:-nova}" "$*" >&2; exit 1; }

# need CMD "how to install it" — fail with a copy-pasteable fix.
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' is not installed. Fix: $2"; }

# cfg KEY — read a top-level string from nova.config.json (needs node or python3).
cfg() {
  if command -v node >/dev/null 2>&1; then
    node -e 'const c=require(process.argv[1]);const v=c[process.argv[2]];if(v===undefined)process.exit(1);console.log(v)' "$ROOT/nova.config.json" "$1"
  else
    python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$ROOT/nova.config.json" "$1"
  fi
}

# Map ARCH (Debian names) to Go / QEMU names.
arch_go()   { case "${1:-amd64}" in amd64) echo amd64;; arm64) echo arm64;; *) die "unsupported ARCH '$1' (amd64 or arm64)";; esac; }
arch_qemu() { case "${1:-amd64}" in amd64) echo x86_64;; arm64) echo aarch64;; *) die "unsupported ARCH '$1'";; esac; }

# Trace every command when VERBOSE=1, so failures can be pasted back with full context.
if [[ "${VERBOSE:-0}" == 1 ]]; then set -x; fi
