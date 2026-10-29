#!/usr/bin/env bash
# Builds novad as one static binary: daemon/bin/novad-<ARCH> (and daemon/bin/novad for the host).
. "$(dirname "$0")/lib.sh"
NOVA_STEP=daemon
ARCH="${ARCH:-amd64}"
need go "install Go 1.22+ (Debian: sudo apt install golang-go; macOS: brew install go)"
version="$(cfg version)"; name="$(cfg name)"
cd "$ROOT/daemon"
mkdir -p bin
log "testing"
[[ "${SKIP_TESTS:-0}" == 1 ]] || go test ./...
log "building novad $version for linux/$ARCH"
CGO_ENABLED=0 GOOS=linux GOARCH="$(arch_go "$ARCH")" go build -trimpath \
  -ldflags "-s -w -X main.version=$version -X 'main.brand=$name'" -o "bin/novad-$ARCH" ./cmd/novad
if [[ "$(uname -s)" == Linux && "$(arch_go "$ARCH")" == "$(go env GOHOSTARCH)" ]]; then cp "bin/novad-$ARCH" bin/novad; fi
log "built daemon/bin/novad-$ARCH ($(du -h "bin/novad-$ARCH" | cut -f1))"
