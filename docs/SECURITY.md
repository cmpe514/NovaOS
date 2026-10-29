# NovaOS security model

NovaOS is a single-user appliance. The goal is that **nothing reachable from the network, and no website
opened in the Browser app, can read the user's data or control the device**, and that release images contain
no passwords, consoles or remote access.

## Processes and privileges

| Process | User | Notes |
|---|---|---|
| systemd, NetworkManager, BlueZ, udisks2, logind, polkit | root / system users | Stock Debian services |
| `novaos-data.service` (`/usr/lib/novaos/mount-data`) | root | Mounts NOVA-DATA by label, runs `e2fsck -p`. Never formats |
| `novaos-storage-setup@DISK.service` | root | Formats one disk, only after the checks below |
| sway, `novad`, `novad bar`, `novad watch`, both Chromium instances, PipeWire | `nova` (uid 1000) | `nova` has a locked password and `/usr/sbin/nologin` as its shell, and is not in `sudo` |

Chromium always runs with its sandbox (Debian's build uses unprivileged user namespaces).
`--no-sandbox` is never used. The only exception is outside the image: `tools/render-splash.mjs` renders
a PNG at build time with Playwright's default settings.

## novad (the local daemon)

- **Loopback only.** `novad` refuses to start unless `--listen` is a loopback address. Nothing else on the
  image listens on a network socket in release builds. The exceptions are the DHCP client run by
  NetworkManager and, in debug builds only, SSH.
- **Authentication.** At start-up `novad` creates a random 256-bit session secret and a 256-bit one-time login
  code. The code is written to `$XDG_RUNTIME_DIR/novaos/login-code`, mode 0600 in a 0700 directory. The
  session launcher opens `/auth?code=…`, and `novad` swaps the code for an `HttpOnly; SameSite=Strict`
  cookie and immediately writes a new code. A code seen once, for example in `ps` output, is useless.
- **Every request** must have `Host` equal to `127.0.0.1:<port>` or `localhost:<port>`, which defeats DNS
  rebinding. Anything with `Sec-Fetch-Site: cross-site` is refused.
- **Every `/api/*` request** must also meet four conditions. `Origin`, when present, must equal the shell's
  origin exactly. `Sec-Fetch-Site`, when present, must be `same-origin`. The session cookie must be valid.
  And except for image GETs, the request must carry the header `X-Nova-Request: 1`, which no
  cross-origin page can send without a CORS preflight.
- **No CORS.** `OPTIONS` requests are answered 403 without any `Access-Control-*` header, so every
  preflight fails. Tests assert that no response ever carries `Access-Control-Allow-Origin`.
- **Response headers.** Responses send a strict CSP (`script-src 'self'`, no inline scripts, `frame-ancestors 'none'`),
  `X-Content-Type-Options: nosniff`, `Cross-Origin-Resource-Policy: same-origin`, `X-Frame-Options: DENY`
  and `Referrer-Policy: no-referrer`.
- **The Browser app** is a separate Chromium profile (`--user-data-dir` under the user's home), so it never
  holds the shell's cookie. Tests open an "attacker" page in the *same* profile as the shell and show that
  reading data and calling `/api/power` both fail (`tests/e2e/device.spec.js`).

## System actions

- **Allow-listed tools only.** `novad` runs only `systemctl`, `nmcli`, `bluetoothctl`, `rfkill`, `wpctl`,
  `busctl`, `timedatectl`, `lsblk`, `findmnt`, `udisksctl`, `chromium` and `swaymsg`, by absolute path,
  with an argument vector (never a shell), a fixed environment and a timeout
  (`daemon/internal/system/runner.go`).
- **Validated arguments.** Every argument is checked before use (`validate.go`): URLs must be http or https
  with a host and no credentials or spaces. Disk and partition names, Bluetooth MAC addresses and backlight
  names must match strict patterns. Time zones must be in `timedatectl list-timezones`. SSIDs and Wi-Fi
  passwords must fit 802.11 limits. Unit tests run the Linux backend against stub tools and assert that no
  invalid value ever reaches a command.
- **Wi-Fi passwords** are passed to NetworkManager through a 0600 `passwd-file`, never on a command line.
- **polkit** (`/etc/polkit-1/rules.d/50-novaos.rules`) grants the `nova` user, and only while its session is
  the active local one, exactly these actions: power off and reboot, set the time zone, Wi-Fi control,
  mounting and ejecting removable drives, and starting `novaos-storage-setup@<disk>.service` for a disk
  name matching the same strict pattern. Everything else falls back to Debian's defaults, which require an
  administrator, and there is none.
- **Storage setup** is a privileged helper. It refuses any disk that has partitions, a partition table,
  any filesystem signature (`lsblk`, `wipefs`, `blkid -p`), is mounted, read-only, smaller than 1 GiB, or
  holds the boot medium. The shell only offers disks that pass the same test, and the user must type the
  disk name to confirm.
- **Files app.** It only touches regular, non-hidden files directly inside `~/Documents`. Names with `/`,
  `\`, control characters or a leading dot are rejected, symlinks are ignored, and a new file never
  overwrites an existing one. USB imports accept only paths that the drive listing itself produced.

## Data at rest

- **Atomic writes.** The writer creates a temporary file, fsyncs it, renames it into place and fsyncs the
  directory. A file that fails to parse is never overwritten: the store becomes read-only and the UI shows
  the error.
- **No encryption.** NOVA-DATA is **not encrypted**. Anyone with physical access to the device or the disk
  can read it. Disk encryption with a PIN is listed under future work.
- **Recovery entry.** The *Recovery* boot entry (release builds included) offers only a fixed menu: check
  and repair, back up to USB, reset settings, information, restart and shut down. It has no shell. Like
  any unencrypted system, physical access lets someone edit the boot command line. See "Hardening options".

## Release vs. debug images

| | Release (`make iso`) | Debug (`make iso DEBUG=1`) |
|---|---|---|
| Login prompts or consoles | none (getty units masked, `NAutoVTs=0`) | tty2 for user `dev` |
| SSH | not installed | `openssh-server`, key from `out/debug_authorized_keys`, or a random per-build password in `out/*.dev-password` |
| sudo | not installed | `dev` has passwordless sudo |
| Chromium DevTools | disabled by policy | enabled |
| Default passwords | none | none (the password is random per build) |

## Network traffic

- **The shell.** It makes no third-party requests. The UI loads nothing remote: the Playwright suite fails
  on any request to a host other than 127.0.0.1.
  - **Chromium's own background traffic** is a separate problem. Even with every relevant switch off
    (background networking, component updates, sync, pings, translation, media router), a net-log
    showed it contacting `accounts.google.com`, `android.clients.google.com/checkin`,
    `update.googleapis.com`, `clients2.google.com` and `www.google.com` within seconds.
  - **The fix:** the shell browser is therefore started with `--proxy-server=http://127.0.0.1:9` (a
    port nobody listens on) and resolver rules that make every host name except localhost fail.
    Loopback bypasses the proxy, so only `novad` is reachable.
  - **The test:** `tests/session/shell-network-test.sh` records a net-log and fails on any TCP
    connection that leaves the machine.
- **Weather.** No request is made until the user chooses a forecast location. After that `novad`
  (not the browser) asks Open-Meteo at most every 15 minutes.
- **The system.** It contacts Debian's NTP pool (`systemd-timesyncd`) to set the clock. NetworkManager's
  connectivity check is disabled. Wi-Fi scans use a random MAC address.
- **The Browser app.** Websites you open load what they load. Managed policies disable extensions,
  printing, sign-in, sync, metrics, spellcheck and `file://` URLs.

## Known residual risks

1. **Same user for the browser and the shell.** Both Chromium instances run as `nova`. A full compromise
   of the *web* browser's main process (a sandbox escape) would therefore have the same rights as `novad`'s
   user. That means it could read user data and use the polkit actions above. Running the web browser as a
   second unprivileged user would close this gap, and is future work.
2. **Physical access** gives access to the data (no encryption, editable boot command line, no Secure Boot
   by default).
3. **Unverified on hardware.** The image was not built or booted in the development sandbox. See the README
   status table.

## Hardening options

- **Secure Boot.** Debian's signed shim and GRUB are what make Secure Boot work. The image does not set up
  shim yet, so disable Secure Boot in the firmware (README → Troubleshooting) or add shim-signed and test
  it yourself.
- **Lock the boot menu.** To lock the boot menu against command-line edits, add a GRUB superuser to
  `image/config/bootloaders/grub-pc/grub.cfg` (`set superusers=…`,
  `password_pbkdf2 … <grub-mkpasswd-pbkdf2 hash>`) and add `--unrestricted` to the three menu entries.

## Reporting

Open a private security advisory on the GitHub repository (Security → Advisories).
