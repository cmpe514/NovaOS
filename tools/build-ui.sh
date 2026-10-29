#!/usr/bin/env bash
# Builds the offline shell UI into ui/dist. Idempotent; reinstalls node modules only when the lockfile changed.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=ui
need node "install Node.js 20+ (Debian: sudo apt install nodejs npm; macOS: brew install node)"
need npm  "install npm (Debian: sudo apt install npm)"
cd "$ROOT/ui"
stamp=node_modules/.package-lock.sha256
want="$(sha256sum package-lock.json | cut -d' ' -f1)"
if [[ ! -f "$stamp" || "$(cat "$stamp")" != "$want" ]]; then
  log "installing pinned build dependencies (npm ci)"
  npm ci --no-audit --no-fund
  echo "$want" > "$stamp"
fi
if [[ "${FETCH_ASSETS:-1}" == 1 ]]; then
  log "fetching original photos (falls back to bundled artwork when offline)"
  node tools/fetch-assets.mjs || warn "asset fetch failed; using fallback artwork"
fi
node tools/build.mjs
log "UI ready in ui/dist"
