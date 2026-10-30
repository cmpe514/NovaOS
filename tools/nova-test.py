#!/usr/bin/env python3
"""NovaOS system test: boots the real images in QEMU and drives them through QMP.

Checks serial markers AND screen contents (screendumps compared with committed golden images), and drives
the UI with real PS/2 key events. Results are written to OUT/results.json and OUT/*.png.

  tools/nova-test.py --arch x86_64 --iso out/novaos-...iso --recovery-iso out/...-recovery-default.iso --out DIR
  tools/nova-test.py --arch aarch64 --kernel out/novaos-...-qemu-aarch64.elf --out DIR
Both architectures are compared with the same golden screenshots (tests/golden/).
"""
import argparse, json, os, socket, struct, subprocess, sys, tempfile, time

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "tests", "golden")
OVMF = "/usr/share/ovmf/OVMF.fd"
RTC = "2026-10-29T17:39:00"   # fixed clock so screenshots are comparable


class VM:
    def __init__(self, machine, out, name):
        """machine: QEMU arguments selecting the system and boot medium (see main)."""
        self.name, self.out = name, out
        self.dir = tempfile.mkdtemp(prefix="nova-")
        self.serial = os.path.join(out, f"{name}.serial.log")
        self.qmp_path = os.path.join(self.dir, "qmp.sock")
        cmd = machine + ["-m", "512", "-display", "none", "-no-reboot",
                         "-rtc", f"base={RTC},clock=vm", "-serial", f"file:{self.serial}",
                         "-qmp", f"unix:{self.qmp_path},server=on,wait=off"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            if os.path.exists(self.qmp_path):
                break
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(self.qmp_path)
        self.f = self.sock.makefile("rw")
        self.f.readline()
        self.qmp("qmp_capabilities")

    def qmp(self, cmd, **args):
        self.f.write(json.dumps({"execute": cmd, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                if "error" in r:
                    raise RuntimeError(f"QMP {cmd}: {r['error']}")
                return r["return"]

    def log(self):
        try:
            with open(self.serial, "rb") as fh:
                return fh.read().decode("utf-8", "replace")
        except FileNotFoundError:
            return ""

    def wait(self, marker, timeout=90, after=0):
        end = time.time() + timeout
        while time.time() < end:
            if marker in self.log()[after:]:
                return True
            if self.proc.poll() is not None:
                return marker in self.log()[after:]
            time.sleep(0.25)
        return False

    def keys(self, *seq):
        """Each item is a qcode or a list of qcodes pressed together (e.g. ['shift', '8'])."""
        for k in seq:
            ks = k if isinstance(k, list) else [k]
            self.qmp("send-key", keys=[{"type": "qcode", "data": x} for x in ks])
            time.sleep(0.25)

    def shot(self, label):
        ppm = os.path.join(self.dir, label + ".ppm")
        self.qmp("screendump", filename=ppm)
        for _ in range(50):
            if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
                break
            time.sleep(0.1)
        time.sleep(0.3)
        png = os.path.join(self.out, f"{self.name}-{label}.png")
        Image.open(ppm).convert("RGB").save(png)
        return png

    def close(self):
        try:
            self.qmp("quit")
        except Exception:
            pass
        try:
            self.proc.wait(10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def screen_stats(png):
    im = Image.open(png).convert("L")
    hist = im.histogram()
    distinct = sum(1 for h in hist if h)
    return im.size, distinct


def compare(png, golden_name, update):
    golden = os.path.join(GOLDEN, golden_name)
    if update or not os.path.exists(golden):
        os.makedirs(GOLDEN, exist_ok=True)
        Image.open(png).save(golden)
        return {"golden": golden_name, "status": "updated"}
    a, b = Image.open(png).convert("RGB"), Image.open(golden).convert("RGB")
    if a.size != b.size:
        return {"golden": golden_name, "status": "size-mismatch", "size": a.size, "golden_size": b.size}
    diff = ImageChops.difference(a, b).convert("L")
    pixels = diff.get_flattened_data() if hasattr(diff, "get_flattened_data") else diff.getdata()
    changed = sum(1 for p in pixels if p > 24)
    ratio = changed / (a.size[0] * a.size[1])
    return {"golden": golden_name, "status": "ok" if ratio < 0.01 else "different", "changed_ratio": round(ratio, 5)}


def main():
    os.environ.setdefault("PYTHONWARNINGS", "ignore")
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", choices=["x86_64", "aarch64"], default="x86_64")
    ap.add_argument("--iso")
    ap.add_argument("--recovery-iso")
    ap.add_argument("--kernel")
    ap.add_argument("--out", required=True)
    ap.add_argument("--update-golden", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    results = []
    disk = os.path.join(a.out, "data-disk.img")
    with open(disk, "wb") as fh:          # a fresh, blank 1 MiB data disk for this run
        fh.truncate(1 << 20)
    drive = ["-drive", f"file={disk},if=none,id=d0,format=raw"]
    if a.arch == "x86_64":
        kvm = ["-enable-kvm"] if os.access("/dev/kvm", os.W_OK) else []
        pc = ["qemu-system-x86_64", "-device", "VGA"] + kvm + drive + ["-device", "virtio-blk-pci,drive=d0,disable-modern=on"]
        normal, recovery, uefi = pc + ["-cdrom", a.iso], pc + ["-cdrom", a.recovery_iso], pc + ["-bios", OVMF, "-cdrom", a.iso]
    else:
        virt = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72", "-device", "ramfb",
                "-device", "virtio-keyboard-device", "-device", "virtio-tablet-device", "-global", "virtio-mmio.force-legacy=false"] \
               + drive + ["-device", "virtio-blk-device,drive=d0", "-kernel", a.kernel]
        normal, recovery, uefi = virt, virt + ["-fw_cfg", "name=opt/nova/cmdline,string=nova.recovery"], None

    def click(vm, x, y):
        """Moves the pointer to framebuffer pixel (x, y) and clicks: relative PS/2 on x86, absolute tablet on ARM."""
        if a.arch == "x86_64":
            # QEMU only sends queued PS/2 motion when the next input event arrives, so move in small steps.
            def move(dx, dy):
                steps = max(abs(dx), abs(dy)) // 100 + 1
                for i in range(steps):
                    vm.qmp("input-send-event", events=[{"type": "rel", "data": {"axis": "x", "value": dx * (i + 1) // steps - dx * i // steps}},
                                                       {"type": "rel", "data": {"axis": "y", "value": dy * (i + 1) // steps - dy * i // steps}}])
                    time.sleep(0.05)
            move(-1500, -1000)
            move(x, y)
        else:
            vm.qmp("input-send-event", events=[{"type": "abs", "data": {"axis": "x", "value": (x * 32768 + 640) // 1280}},
                                               {"type": "abs", "data": {"axis": "y", "value": (y * 32768 + 400) // 800}}])
        time.sleep(0.4)
        for down in (True, False):
            vm.qmp("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": "left"}}])
            time.sleep(0.3)

    def slots():
        """(generation or None) for slots A and B, read straight from the disk image."""
        out = []
        with open(disk, "rb") as fh:
            for base in (8, 136):
                fh.seek(base * 512)
                h = fh.read(32)
                out.append(struct.unpack_from("<Q", h, 16)[0] if h[:8] == b"NOVASNAP" else None)
        return out

    def check(name, ok, evidence=""):
        results.append({"test": name, "result": "PASS" if ok else "FAIL", "evidence": evidence})
        print(("PASS " if ok else "FAIL ") + name + (f"  ({evidence})" if evidence else ""), flush=True)
        return ok

    def visual(vm, label, golden):
        png = vm.shot(label)
        size, distinct = screen_stats(png)
        c = compare(png, golden, a.update_golden)
        check(f"{vm.name}: {label} screenshot matches golden", distinct > 32 and c["status"] in ("ok", "updated"),
              f"{os.path.basename(png)} {size[0]}x{size[1]} {distinct} grey levels, {c}")

    # 1. BIOS boot to the shell, self-tests, UI interaction, power off.
    vm = VM(normal, a.out, "boot")
    try:
        check("boot: kernel ready", vm.wait("NOVA_KERNEL_READY", 120))
        check("boot: userspace ready", vm.wait("NOVA_USERSPACE_READY"))
        ok = vm.wait("NOVA_SELFTEST_DONE")
        log = vm.log()
        for line in log.splitlines():
            if line.startswith("NOVA_SELFTEST "):
                _, name, res = line.split()
                check("selftest: " + name, res == "PASS", "kernel serial log")
        check("boot: all self-tests passed", ok and "NOVA_SELFTEST_DONE PASS" in log)
        check("boot: shell ready", vm.wait("NOVA_SHELL_READY"))
        time.sleep(1)
        visual(vm, "home", "home.png")

        n = len(vm.log())
        vm.keys("right", "right", "ret")
        check("ui: arrow keys + Enter open Calculator", vm.wait("NOVA_SHELL_VIEW calculator", 20, n))
        n = len(vm.log())
        vm.keys("1", "2", ["shift", "8"], "3", "ret")
        check("ui: calculator computes 12*3", vm.wait("NOVA_CALC 36\r\n", 20, n) or vm.wait("NOVA_CALC 36", 5, n))
        vm.keys("slash", "8", "ret")
        check("ui: calculator computes 36/8", vm.wait("NOVA_CALC 4.5", 20, n))
        visual(vm, "calculator", "calculator.png")

        n = len(vm.log())
        vm.keys("esc")
        check("ui: Esc returns home", vm.wait("NOVA_SHELL_VIEW home", 20, n))
        n = len(vm.log())
        vm.keys("left", "ret")   # focus moves from Calculator (2) to Tasks (1)
        check("ui: open Tasks", vm.wait("NOVA_SHELL_VIEW tasks", 20, n))
        vm.keys("m", "i", "l", "k", "ret", "e", "g", "g", "s", "ret", "up", "spc")
        check("ui: add two tasks and complete one", vm.wait("NOVA_TASKS total=2 done=1", 20, n))
        vm.keys("delete")
        check("ui: delete a task", vm.wait("NOVA_TASKS total=1 done=0", 20, n))
        visual(vm, "tasks", "tasks.png")

        n = len(vm.log())
        vm.keys("esc", "up", "ret")   # Tasks (1) -> up -> Quick settings -> Settings
        check("ui: Quick settings opens Settings", vm.wait("NOVA_SHELL_VIEW settings", 20, n))
        time.sleep(0.5)
        png = vm.shot("settings")
        check("boot: settings screenshot captured", screen_stats(png)[1] > 32, os.path.basename(png))
        vm.keys("right", "right", "ret")
        check("settings: 24-hour clock turned on", vm.wait("NOVA_SETTING clock24=1", 20, n))
        check("storage: changes saved to the data disk", "NOVA_STORE_SAVED" in vm.log())
        vm.keys("left", "ret")
        check("power: Shut down from Settings powers off", vm.wait("NOVA_POWER_OFF", 20, n))
        try:
            vm.proc.wait(20)
            check("power: QEMU exited after power off", True, f"exit code {vm.proc.returncode}")
        except subprocess.TimeoutExpired:
            check("power: QEMU exited after power off", False)
    finally:
        vm.close()

    # 2. Persistence after a full power cycle, and pointer input.
    vm = VM(normal, a.out, "reboot")
    try:
        check("persist: tasks and settings survive a restart",
              vm.wait("NOVA_STATE_LOADED store=yes tasks=1 note_len=0 clock24=1", 120), "serial: NOVA_STATE_LOADED")
        time.sleep(1)
        n = len(vm.log())
        click(vm, 873, 331)                       # Notes tile
        check("pointer: click opens Notes", vm.wait("NOVA_SHELL_VIEW notes", 20, n))
        vm.keys("h", "i")
        n = len(vm.log())
        click(vm, 112, 106)                       # back button
        check("pointer: click on back returns home", vm.wait("NOVA_SHELL_VIEW home", 20, n))
        check("persist: note saved", "NOVA_STORE_SAVED" in vm.log()[n - 2000:] or "NOVA_STORE_SAVED" in vm.log())
    finally:
        vm.close()

    # 3. Interrupted write: damage the newest snapshot the way a torn write would; the previous one must load.
    gens = slots()
    newest = 0 if (gens[0] or 0) > (gens[1] or 0) else 1
    with open(disk, "r+b") as fh:
        fh.seek((8 + newest * 128 + 1) * 512 + 100)
        b = fh.read(1); fh.seek(-1, 1); fh.write(bytes([b[0] ^ 0xff]))
    vm = VM(normal, a.out, "torn-write")
    try:
        ok = vm.wait("NOVA_STORE_LOADED", 120)
        want = f"NOVA_STORE_LOADED slot={'AB'[1 - newest]} gen={gens[1 - newest]}"
        check("storage: damaged newest snapshot is rejected, previous one loads", ok and want in vm.log(), f"slots {gens}, expected '{want}'")
        check("storage: shell starts with the previous state", vm.wait("NOVA_STATE_LOADED store=yes tasks=1"))
    finally:
        vm.close()

    # 4. Recovery boot entry (independent program, same theme), and "Boot normal system".
    vm = VM(recovery, a.out, "recovery")
    try:
        check("recovery: kernel ready in recovery mode", vm.wait("NOVA_KERNEL_READY", 120) and "mode=1" in vm.log())
        check("recovery: self-tests pass", vm.wait("NOVA_SELFTEST_DONE PASS"))
        check("recovery: graphical recovery ready", vm.wait("NOVA_RECOVERY_READY graphical"))
        check("recovery: shell is not started", "NOVA_SHELL_READY" not in vm.log())
        time.sleep(1)
        n = len(vm.log())
        vm.keys(*["down"] * 9, "ret")            # Check and repair storage
        check("recovery: check and repair rewrites the damaged slot", vm.wait("NOVA_RECOVERY_REPAIRED", 30, n))
        g = slots()
        check("recovery: both snapshot slots valid after repair", g[0] is not None and g[1] is not None, f"slots {g}")
        n = len(vm.log())
        vm.keys("down", "ret")                    # Reset settings
        check("recovery: reset settings", vm.wait("NOVA_RECOVERY_SETTINGS_RESET", 30, n))
        vm.keys("down", "ret", "e", "r", "a", "s", "x", "ret")
        time.sleep(1)
        check("recovery: factory reset refuses a wrong confirmation", "NOVA_STORE_ERASED" not in vm.log())
        vm.keys("ret", "e", "r", "a", "s", "e", "ret")
        check("recovery: factory reset with typed confirmation erases the data", vm.wait("NOVA_STORE_ERASED", 30, n))
        visual(vm, "menu", "recovery.png")
        n = len(vm.log())
        vm.keys(*["up"] * 11, "ret")
        check("recovery: 'Boot normal system' starts the shell", vm.wait("NOVA_SHELL_READY", 30, n))
        check("recovery: shell starts empty after factory reset", vm.wait("NOVA_STATE_LOADED store=yes tasks=0 note_len=0 clock24=0", 10, n))
    finally:
        vm.close()

    # 5. UEFI boot of the same ISO.
    if uefi and os.path.exists(OVMF):
        vm = VM(uefi, a.out, "uefi")
        try:
            check("uefi: kernel ready", vm.wait("NOVA_KERNEL_READY", 180))
            check("uefi: self-tests pass", vm.wait("NOVA_SELFTEST_DONE PASS"))
            check("uefi: shell ready", vm.wait("NOVA_SHELL_READY"))
            time.sleep(1)
            png = vm.shot("home")
            size, distinct = screen_stats(png)
            check("uefi: home screen rendered", distinct > 32, f"{size[0]}x{size[1]}, {distinct} grey levels")
        finally:
            vm.close()
    elif uefi:
        check("uefi: OVMF firmware present", False, f"{OVMF} missing; install ovmf")

    with open(os.path.join(a.out, "results.json"), "w") as fh:
        json.dump(results, fh, indent=1)
    failed = [r for r in results if r["result"] != "PASS"]
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed")
    sys.exit(1 if failed else 0)


main()
