#!/usr/bin/env bash
# Runs a make target inside the NovaOS builder container (Docker or Podman), or directly when
# NATIVE=1 / already inside the container. live-build needs --privileged (chroot, mounts, binfmt).
#   tools/in-builder.sh iso
. "$(dirname "$0")/lib.sh"
NOVA_STEP=builder
target="${1:?usage: in-builder.sh <make target>}"; shift || true
if [[ -f /.novaos-builder || "${NATIVE:-0}" == 1 ]]; then
  exec make -C "$ROOT" "$target" NATIVE=1 "$@"
fi
engine="${ENGINE:-}"
if [[ -z "$engine" ]]; then engine="$(command -v podman || command -v docker || true)"; fi
[[ -n "$engine" ]] || die "neither podman nor docker found. Install Docker (https://docs.docker.com/get-docker/) or Podman, or build natively on Debian 13 with NATIVE=1 (see docs/BUILDING.md)."
"$engine" info >/dev/null 2>&1 || die "$engine is installed but not running (start Docker Desktop, or: sudo systemctl start docker)."
tag="novaos-builder:$(sha256sum "$ROOT/tools/builder/Containerfile" | cut -c1-12)"
if ! "$engine" image inspect "$tag" >/dev/null 2>&1; then
  log "building the builder image $tag (first run only, a few minutes)"
  "$engine" build -t "$tag" -f "$ROOT/tools/builder/Containerfile" "$ROOT/tools/builder"
fi
tty=(); [[ -t 0 && -t 1 ]] && tty=(-t)
# live-build works in a named volume (a real Linux filesystem even on macOS/Windows), results are
# copied to ./out by the iso script.
log "running 'make $target' in $tag"
exec "$engine" run --rm -i "${tty[@]}" --privileged \
  -v "$ROOT:/src" -v novaos-build:/build -v novaos-cache:/cache \
  -e ARCH -e VERBOSE -e DEBUG -e FETCH_ASSETS -e NOVA_MIRROR -e GOCACHE=/cache/go -e GOMODCACHE=/cache/gomod -e npm_config_cache=/cache/npm \
  -w /src "$tag" make "$target" NATIVE=1 "$@"
