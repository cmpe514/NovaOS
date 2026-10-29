# NovaOS — reproducible build of a Debian-based appliance OS that boots straight into the NovaOS shell.
# `make help` lists everything. Heavy targets run in a privileged builder container (Docker or Podman);
# NATIVE=1 runs them on the host instead (Debian 13 only, see docs/BUILDING.md).
SHELL       := /bin/bash
.DEFAULT_GOAL := help
ARCH        ?= amd64
DEBUG       ?= 0
NATIVE      ?= 0
VERBOSE     ?= 0
export ARCH DEBUG VERBOSE NATIVE

INB := tools/in-builder.sh

.PHONY: help doctor ui daemon dev iso run run-uefi usb test test-go test-ui smoke assets clean distclean zip

help: ## show this help
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  \033[1mmake %-10s\033[0m %s\n", $$1, $$2}'
	@echo; echo "  Variables: ARCH=amd64|arm64  DEBUG=1 (dev console + SSH)  NATIVE=1 (no container)  VERBOSE=1 (trace)"

doctor: ## check prerequisites and print the exact commands to fix anything missing
	@tools/doctor.sh

ifeq ($(NATIVE),1)
ui: ## build the offline shell UI into ui/dist
	tools/build-ui.sh
daemon: ## test and build novad (static binary) into daemon/bin
	tools/build-daemon.sh
iso: ui daemon ## build the bootable hybrid ISO into out/ (BIOS + UEFI)
	tools/build-iso.sh
test-go:
	cd daemon && go test -race ./...
test-ui: ui
	tools/test-ui.sh
test: test-go test-ui ## run all tests: Go unit tests, Playwright parity/offline/visual/device suites
smoke: ## boot the ISO headless in QEMU and wait for NOVAOS_READY + a non-blank screenshot
	tools/smoke-qemu.sh
else
ui daemon iso test test-go test-ui smoke:
	$(INB) $@
endif

assets: ## download the original wallpaper/photos (optional; fallback art is bundled)
	cd ui && node tools/fetch-assets.mjs

dev: ## run the shell + novad on this desktop (fake device) and open it in Chromium
	tools/dev.sh

run: ## boot the ISO in QEMU (BIOS) with a persistent data disk and sound
	tools/run-qemu.sh bios

run-uefi: ## boot the ISO in QEMU (UEFI)
	tools/run-qemu.sh uefi

usb: ## write the ISO to a USB stick: make usb DEV=/dev/sdX (asks you to type the device again)
	@test -n "$(DEV)" || { echo "usage: make usb DEV=/dev/sdX   (find it with: lsblk -d -o NAME,SIZE,MODEL,TRAN)"; exit 2; }
	sudo tools/write-usb.sh "$(DEV)"

zip: ## package the source tree (without build outputs) as out/novaos-src.zip
	@mkdir -p out && git archive --format=zip -o out/novaos-src.zip HEAD && echo "out/novaos-src.zip"

clean: ## remove build outputs (keeps downloaded caches)
	rm -rf ui/dist daemon/bin out tests/results tests/report tests/reference/.build tests/.novad .dev-data

distclean: clean ## also remove node_modules, the live-build tree and the builder volumes
	rm -rf ui/node_modules tests/node_modules build
	-docker volume rm novaos-build novaos-cache 2>/dev/null; -podman volume rm novaos-build novaos-cache 2>/dev/null; true
