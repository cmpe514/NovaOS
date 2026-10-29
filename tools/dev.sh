#!/usr/bin/env bash
# Development loop on a normal Linux (or macOS) desktop: builds the UI and novad, runs novad with the
# fake system backend and opens the shell in a Chromium app window. Ctrl+C stops everything.
#   PORT=7171 SYSTEM=fake|linux OPEN=1 tools/dev.sh
# SYSTEM=linux reads the real machine's status; power and disk setup stay simulated in --dev mode.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=dev
PORT="${PORT:-7171}"
DATA="${NOVA_DEV_DATA:-$ROOT/.dev-data}"
SYSTEM="${SYSTEM:-fake}"
FETCH_ASSETS="${FETCH_ASSETS:-0}" "$ROOT/tools/build-ui.sh"
need go "install Go 1.22+ (Debian: sudo apt install golang-go; macOS: brew install go)"
(cd "$ROOT/daemon" && CGO_ENABLED=0 go build -o bin/novad-dev ./cmd/novad)
mkdir -p "$DATA/data" "$DATA/home" "$DATA/run"
chmod 700 "$DATA" "$DATA/run"
bin="$ROOT/daemon/bin/novad-dev"
"$bin" serve --dev --system "$SYSTEM" --listen "127.0.0.1:$PORT" --ui "$ROOT/ui/dist" \
  --data "$DATA/data" --home "$DATA/home" --runtime-dir "$DATA/run" &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT INT TERM
"$bin" wait --listen "127.0.0.1:$PORT" --timeout 20s
url="$("$bin" login-url --listen "127.0.0.1:$PORT" --runtime-dir "$DATA/run")"
log "data lives in $DATA (delete it to start fresh)"
if [[ "${OPEN:-1}" == 1 ]]; then
  for b in chromium chromium-browser google-chrome google-chrome-stable "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"; do
    if command -v "$b" >/dev/null 2>&1 || [[ -x "$b" ]]; then
      log "opening the shell in $b"
      "$b" --user-data-dir="$DATA/chromium-profile" --no-first-run --app="$url" >/dev/null 2>&1 &
      opened=1; break
    fi
  done
  [[ -n "${opened:-}" ]] || log "no Chromium found; open this URL in any browser: $url"
else
  log "open this URL: $url"
fi
wait "$pid"
