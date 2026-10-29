# NovaOS architecture

## Decision record

1. **Base:** Debian 13 "trixie" (current stable), built with live-build. It has the best hardware and firmware coverage, and `non-free-firmware` is enabled for Wi-Fi and GPU blobs.
2. **Image:** a hybrid ISO (`iso-hybrid`) that boots with GRUB on both BIOS and UEFI (amd64). On arm64 it boots with GRUB on UEFI only. `ARCH` is a build parameter.
3. **Boot:** systemd and Plymouth, with quiet kernel arguments. `novaos-session.service` runs the session as the unprivileged user `nova` on tty1 through PAM. There is no display manager and no getty.
4. **Compositor:** sway, configured as a kiosk. cage was rejected because the Browser app needs a second window and a guaranteed way back to the shell, and sway's IPC provides that.
5. **Rendering:** wlroots GLES2 is used when a GPU render node works. Otherwise sway falls back to pixman software rendering. That happens automatically when there is no render node, or after one fast crash on the GPU path, and on demand with `novaos.renderer=pixman` (safe mode).
6. **Shell:** Debian's `chromium` package in `--app` mode, with a per-boot profile in RAM. Every rebuild picks up Debian's security updates, and there is no bundled engine to maintain. Electron and Tauri were rejected because of the patch lag.
7. **Browser app:** a second Chromium instance with its own persistent profile (`--class=novaos-web`) on sway workspace 2. It never has the shell's session cookie.
8. **Daemon:** `novad`, a single static Go binary built from the standard library only. It serves the UI, the data API, the system API and `/healthz`, on 127.0.0.1 only and same-origin with the UI.
9. **Auth:** a per-boot session cookie (HttpOnly, SameSite=Strict) is obtained with a one-time code from a 0600 file. Requests must pass Host, Origin and Sec-Fetch-Site checks and carry a custom header. There is no CORS.
10. **Privilege:** `novad` runs as `nova` inside the logind session. A polkit rule grants exactly the actions it needs: power, timezone, Wi-Fi, udisks, and one storage-setup unit. Commands run through an allow-listed argv runner, never a shell.
11. **Persistence:** an ext4 filesystem labelled `NOVA-DATA` is mounted on `/var/lib/novaos` before the session starts. It holds data, home, Wi-Fi profiles, Bluetooth pairings and the time zone. Without it the data stays in RAM and Settings says so.
12. **Storage setup:** only provably blank disks are offered. The user must type the disk name, and a root helper unit re-checks that the disk is blank before it writes anything.
13. **Data format:** atomic JSON (temp file, fsync, rename, directory fsync). A damaged file is never overwritten. Files-app items are real files in `~/Documents`.
14. **UI:** the original HTML/CSS/JS, kept verbatim except where the offline build requires a change. Everything is vendored and pinned, with checksums. `window.dataSdk` keeps its original contract.
15. **Release vs debug:** release builds have no consoles, no SSH, no passwords and no devtools, but keep **Safe mode** and **Recovery** boot entries. `DEBUG=1` adds a `dev` user, tty2 and SSH.

### Considered and not chosen
- **A from-scratch kernel or OS:** not needed for "boots straight into this UI". If you want one, that is a separate project; this appliance is the practical route.
- **Alpine, Buildroot or mkosi:** each is smaller, but Chromium, firmware coverage and security support are weaker (Alpine/Buildroot) or less familiar (mkosi). Debian live-build is the default the brief asked for.
- **cage:** a single-window kiosk, so it cannot provide the guaranteed Home path for the Browser app.
- **Electron or Tauri:** they bundle or pin a browser engine, so security fixes lag behind Debian's.

## Boot flow

```
firmware (BIOS/UEFI)
  └─ GRUB (hidden menu: NovaOS · Safe mode · Recovery)
      └─ linux + initrd (live-boot) ── Plymouth splash (novaos theme)
          └─ systemd
              ├─ novaos-data.service      find LABEL=NOVA-DATA → e2fsck -p → mount /var/lib/novaos
              │                           bind Wi-Fi/Bluetooth state, restore time zone
              ├─ NetworkManager, bluetooth (after novaos-data)
              └─ novaos-session.service   User=nova, PAM "novaos", tty1, plymouth quit --retain-splash
                  └─ /usr/lib/novaos/session  (renderer choice) → sway --config /etc/novaos/sway/config
                      ├─ novad-run → novad serve (127.0.0.1:7171)
                      ├─ novad watch (bar on/off, return home from an empty browser workspace)
                      ├─ swaybar → novad bar (Home button + clock)
                      ├─ shell → chromium --app=http://127.0.0.1:7171/auth?code=… (restarted if closed)
                      └─ osk → squeekboard (touchscreens only)
```

Once the home screen has rendered, the page calls `POST /api/ready`. `novad` then logs `NOVAOS_READY uptime=… persistent=… boots=… write=…`, and `novaos-ready-marker.service` copies that line to the serial port for the smoke test.

## Repository layout

| Path | What |
|---|---|
| `nova.config.json` | Name, tagline, version, accent palette, user, port: the single place to rename the project |
| `ui/original/` | The uploaded `ui.html`, untouched (it is truncated, see its README) |
| `ui/src/` | The shell UI. Markup, CSS and JS derived from the original, plus `nova-api.js`, `data-sdk.js`, `system.js` and `boot.js` |
| `ui/tools/` | `build.mjs` (offline build), `fetch-assets.mjs`, `serve-static.mjs` |
| `daemon/` | `novad`: `internal/{auth,server,store,system,weather}` and `cmd/novad` |
| `image/` | live-build tree: `auto/`, `config/` (package lists, hooks, bootloader, files for the image) |
| `tools/` | `build-*.sh`, `dev.sh`, `run-qemu.sh`, `smoke-qemu.sh`, `write-usb.sh`, `doctor.sh` and the builder container |
| `tests/` | Playwright suites (preview, device, visual), the headless session test, the reference builder |
| `docs/` | This file, `SECURITY.md`, `BUILDING.md` |

## The UI and the device

`ui/src/js/nova-api.js` decides the mode at start-up:

- **device**: `novad` served the page and the session cookie is valid. Data goes through `novad`, and every system control talks to the device.
- **preview**: a plain static server or `file://`. Data is kept in `localStorage`, and the Wi-Fi and Bluetooth tiles are labelled "Preview".
- **unauthorised**: `novad` served the page, but this browser profile has no session (for example, someone typed `127.0.0.1:7171` into the Browser app). Nothing loads.

`system.js` implements the rule that "every toggle or indicator is real, or hidden, or labelled":

| UI element | On the device | No such hardware | Preview |
|---|---|---|---|
| Wi-Fi tile and icon | NetworkManager radio; icon shows connected, connecting or off, plus Ethernet | tile and Settings row hidden | labelled "Preview" |
| Signal icon | Wi-Fi signal strength | hidden | hidden |
| Battery icon | `/sys/class/power_supply` (peripheral batteries ignored) | hidden | hidden |
| Bluetooth tile | BlueZ power, plus a pairing panel | hidden | labelled "Preview" |
| Brightness | logind `SetBrightness` on `/sys/class/backlight` | original wallpaper dimming | original wallpaper dimming |
| Quiet | PipeWire mute (`wpctl`) and blocks the melody | blocks the melody | blocks the melody |
| Volume slider | PipeWire volume | hidden | hidden |
| Power menu | `systemctl poweroff/reboot` (polkit) | — | hidden |
| Weather | Open-Meteo once a location is chosen | labelled sample | labelled sample |
| Storage | NOVA-DATA status, setup and USB drives | — | hidden |

## Risks and unknowns

The authoritative status list is in the README under "What has been verified". The main open questions are:

- **live-build specifics (UNVERIFIED).** These are: option names in trixie's live-build, the GRUB template override (`config/bootloaders/grub-pc`, with fixed `/live/vmlinuz` paths), `grub-pc` for BIOS on an ISO, and purging `live-config` in a hook. The fallbacks are `NOVA_BOOTLOADERS="syslinux grub-efi"` and the build log.
- **Plymouth handover (UNVERIFIED).** `plymouth quit --retain-splash` should hand the screen to sway cleanly. If you see a flash of console text, add `plymouth.enable=0` to test without the splash.
- **GPU paths (UNVERIFIED on real hardware).** The software fallback is implemented, and the session test proves it works headless.
- **Network services (UNVERIFIED).** These are: Wi-Fi via `nmcli` with `passwd-file`, BlueZ via `bluetoothctl`, PipeWire via `wpctl`, and logind backlight via `busctl`. The command lines are unit-tested against stub tools. They have not run against the real services.
- **ISO size (UNVERIFIED).** `--firmware-chroot true` installs every firmware package, which may push the ISO toward the 1.5 GB target. `make iso` prints the size and warns above it.
- **arm64.** The config path exists (GRUB EFI, `ttyAMA0`, cross-build through qemu-user-static), but it has not been built.
