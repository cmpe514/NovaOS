#!/usr/bin/env bash
# Runs the Playwright suites (preview: parity/offline/visual; device: UI against novad --dev).
# Uses $NOVA_CHROMIUM when set (builder container: Debian's chromium), else Playwright's own Chromium.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=test-ui
need node "install Node.js 20+"
need go "install Go 1.22+ (the device suite starts novad)"
[[ -f "$ROOT/ui/dist/index.html" ]] || "$ROOT/tools/build-ui.sh"
cd "$ROOT/tests"
stamp=node_modules/.package-lock.sha256
want="$(sha256sum package-lock.json | cut -d' ' -f1)"
if [[ ! -f "$stamp" || "$(cat "$stamp")" != "$want" ]]; then npm ci --no-audit --no-fund; echo "$want" > "$stamp"; fi
if [[ -z "${NOVA_CHROMIUM:-}" ]]; then
  log "making sure Playwright's Chromium is installed"
  npx playwright install chromium
fi
CI="${CI:-1}" npx playwright test "$@"
log "HTML report: tests/report/index.html"
