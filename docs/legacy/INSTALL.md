# How to install NovaOS

NovaOS is a complete operating system. You start it from a USB stick or in a virtual machine, and it boots
straight into the NovaOS home screen.

## 1. Get the ISO file

**Download it:** open the repository's **Releases** page (right-hand side of the GitHub page) and download
these two files:

- `novaos-<version>-amd64.iso`
- its `.sha256`

**Or build it yourself:** you need Linux, macOS or Windows with WSL2, plus Docker and about 20 GB free:

```bash
git clone https://github.com/cmpe514/NovaOS && cd NovaOS
```
```bash
make doctor
```
```bash
make iso
```

`make doctor` tells you if anything is missing. The ISO appears in the `out/` folder after 15–40 minutes.

## 2a. Try it in a virtual machine (easiest, nothing on your computer changes)

**VirtualBox** (free, Windows/macOS/Linux):

1. **New VM:** Type *Linux*, Version *Debian (64-bit)*, Memory **3072 MB**, 2 CPUs.
2. **Settings → Display:** Graphics controller **VMSVGA**.
3. **Settings → Storage:** put the ISO in the optical drive, and add a new **8 GB** hard disk.
4. **Settings → Audio:** Intel HD Audio.
5. **Start.** After a few seconds the NovaOS home screen appears.

**QEMU:** with the ISO in `out/`, run:

```bash
make run
```

## 2b. Run it on a real PC from a USB stick

1. Write the ISO to a USB stick (8 GB or more; **everything on the stick is erased**):
   - **Windows or macOS:** use [balenaEtcher](https://etcher.balena.io): *Flash from file* → choose the ISO
     → choose the stick → *Flash*. On Windows, [Rufus](https://rufus.ie) works too (choose *DD Image mode*).
   - **Linux:** run the command below. The script asks you to type the device again to confirm.
     ```bash
     make usb DEV=/dev/sdX
     ```
2. Plug the stick into the PC, switch it on and open the boot menu (usually **F12**, **F11**, **F8** or **Esc**;
   **Option ⌥** on Intel Macs). Choose the USB stick.
3. If the PC refuses to boot the stick, turn off **Secure Boot** in the firmware settings.

## 3. First start: keep your data

With nothing else set up, NovaOS keeps notes, tasks, files and settings in memory, and Settings says
*"Changes won't be saved"*. To keep them:

- **Virtual machine, or a PC with an empty second disk:** open **Settings → Storage → Set up storage…**,
  choose the empty disk and type its name to confirm.
- **USB stick on Linux:** write it with `sudo PERSIST=1 tools/write-usb.sh /dev/sdX`. That adds a storage
  partition to the stick itself.

## 4. Everyday use

- **Home:** the Super/Windows key, the Home key, or the ⌂ Home button at the bottom of the web browser.
- **Turn off:** Quick settings (the sliders icon) → **Power…** → *Shut down*.
- **Problems with the display:** restart, hold **Shift** (or press **Esc**) while it starts, and choose
  **Safe mode**.
- **Repairs and backups:** choose **Recovery** in the same menu.

More detail, including troubleshooting, is in [README.md](README.md).
