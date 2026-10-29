# Building NovaOS

`make iso` turns this repository into `out/novaos-<version>-<arch>.iso`. It needs a Linux kernel and root
privileges, because live-build uses chroot, mounts and debootstrap. There are three ways to provide that.

## 1. Container (recommended: Linux, macOS, Windows/WSL2)

```bash
make doctor
```
```bash
make iso
```

`tools/in-builder.sh` builds the image `novaos-builder:<hash>` from `tools/builder/Containerfile`. That image
is Debian 13 with live-build, Go, Node.js, QEMU and Chromium. It then runs `make iso NATIVE=1` inside the
image with `--privileged`. The live-build tree lives in a Docker volume (`novaos-build`) rather than in your
checkout, which keeps it on a real Linux filesystem even on macOS and Windows. Package downloads are cached
across builds.

- **Linux:** Docker Engine or Podman. With rootless Podman, run `sudo make iso`, because privileged
  containers need root.
- **macOS:** Docker Desktop (or OrbStack, or Colima with `--vm-type vz`). On Apple Silicon the amd64 build
  runs under emulation and takes noticeably longer. See "arm64" below for a native image.
- **Windows:** use WSL2 (Ubuntu or Debian), plus Docker Desktop with *WSL integration* enabled. Run every
  `make` command inside the WSL shell, in a directory inside the Linux filesystem (`~/NovaOS`, not `/mnt/c/...`).

The first build downloads about 1 GB of packages and takes 15–40 minutes. Rebuilds reuse the cache.

## 2. Native, on Debian 13

```bash
sudo apt install live-build debootstrap xorriso squashfs-tools mtools dosfstools grub-pc-bin grub-efi-amd64-bin golang-go nodejs npm rsync python3 make git
```
```bash
make ui daemon NATIVE=1
```
```bash
sudo make iso NATIVE=1
```

## 3. A Debian VM (when containers are not an option)

1. Download the Debian 13 netinst ISO from https://www.debian.org/distrib/ and install it in any VM
   (VirtualBox, UTM, Hyper-V, …). Give it 4 GB RAM and a 40 GB disk, and pick no desktop environment.
2. Inside the VM, run:
   ```bash
   sudo apt install git make
   ```
   ```bash
   git clone https://github.com/cmpe514/NovaOS && cd NovaOS
   ```
3. Follow section 2.
4. Copy `out/*.iso` back to your computer, with `scp` or a shared folder.

## Build options

| Variable | Default | Meaning |
|---|---|---|
| `ARCH` | `amd64` | `arm64` builds a UEFI-only image (see below) |
| `DEBUG` | `0` | `1` adds a `dev` user (tty2 and SSH), sudo and DevTools. Put your public key in `out/debug_authorized_keys` before building, otherwise a random password is written next to the ISO |
| `NOVA_MIRROR` | `http://deb.debian.org/debian/` | a closer or caching Debian mirror (e.g. apt-cacher-ng: `http://host:3142/deb.debian.org/debian/`) |
| `NOVA_BOOTLOADERS` | `grub-pc grub-efi` | fallback if your live-build cannot use GRUB for BIOS: `"syslinux grub-efi"` (the boot menu then comes from live-build's defaults) |
| `FETCH_ASSETS` | `1` | `0` skips downloading the original Pexels photos; the bundled CC0 art is used instead |
| `NOVA_DRY_RUN` | `0` | `1` renders the live-build tree into `build/dry-run-<arch>` and stops. No root is needed, which is useful for reviewing changes |
| `VERBOSE` | `0` | `1` traces every command (paste the output when asking for help) |

### arm64

`make iso ARCH=arm64` needs an arm64 host (e.g. an Apple Silicon Mac in Docker Desktop, or an arm64 Linux
machine). Alternatively, use an amd64 host that has `qemu-user-static` and binfmt registered
(`docker run --privileged --rm tonistiigi/binfmt --install arm64`). The arm64 path is configured but has
**not been built or booted yet**.

## Renaming the project

Edit `nova.config.json` (name, id, tagline, version, accents, hostname, user, port), then:

```bash
make clean
```
```bash
make iso
```

The UI, boot splash (re-rendered with Playwright), boot menu, sway bar, polkit rule, os-release and daemon
all read their values from this file.

## What the build does

1. `tools/build-ui.sh` runs `npm ci` with exact versions (lockfile-pinned), fetches the optional photos,
   checks every vendored file against `ui/vendor.lock.json`, compiles Tailwind and writes `ui/dist`.
2. `tools/build-daemon.sh` runs `go test`, then builds `novad` with `CGO_ENABLED=0`: one static binary.
3. `tools/build-iso.sh` copies `image/` into the build directory and fills in the `%%PLACEHOLDERS%%`
   (`tools/render-templates.py` fails on any unknown one). It adds `novad`, the UI and the licence files,
   then runs `lb config` and `lb build`. Finally it copies the ISO, its `.sha256`, the package list and the
   build log into `out/`.
