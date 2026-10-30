# Nothing Phone (3a) Lite — port feasibility report

**Status: BLOCKED. NovaOS does not run on this phone, and nothing in this repository should be written to
one.** This report separates what sources state from what is still unknown. Nothing here has been
checked on a physical device.

## 1. Identity (sources: public spec listings, not checked on hardware)

| Item | Finding | Confidence / source |
|---|---|---|
| Model | Nothing Phone (3a) Lite, model number **A001T** | Geekbench listing and spec sites ([GSMArena][gsma-news], [GSMArena specs][gsma]) |
| Not to be confused with | Phone (3a) (A059, codename "Asteroids"), Phone (3a) Pro (A059P), CMF Phone 2 Pro | community device tables; verify on device |
| SoC | **MediaTek Dimensity 7300 Pro** (4 nm; Cortex-A78 + Cortex-A55, ARMv8.2-A, 64-bit) | launch coverage ([FoneArena][fonearena]); early leaks said "Dimensity 7300" |
| Device codename | "Galaxian" | **unverified**: a single community-posted table. Must be read from `getprop ro.product.device` on a real device |
| Memory / storage | 8 GB LPDDR4X; 128/256 GB **UFS 2.2** | spec sites |
| Display | 6.77" 1080×2392 AMOLED, 120 Hz | [GSMArena][gsma] |
| Battery | 5000 mAh, 33 W wired | spec sites |
| Board revision | unknown | needs the device |

## 2. Boot chain and installation

| Question | Finding |
|---|---|
| Bootloader unlock available? | **Unknown for this model.** Older Nothing phones (Phone (1)) unlock with `fastboot flashing unlock`. Community posts say the 3a series unlocks easily, but none confirm the 3a **Lite**. |
| Consequences of unlocking | Unknown for this model. Usually a data wipe and a warning screen at boot. Effects on warranty, banking and DRM apps must be confirmed from Nothing's own terms. |
| Boot image format / header version | Unknown. Android 15 MediaTek devices typically use `boot`/`vendor_boot`/`init_boot` with header v4. **Not assumed:** it must be dumped from the device. |
| Device tree requirements | Unknown. MediaTek usually puts DTBs in `vendor_boot` or a `dtbo` partition. |
| Verified boot / rollback | Android Verified Boot 2.0 is expected. Rollback indexes and what the bootloader does with an unsigned kernel when unlocked are unknown. |
| Partition layout and slots | Unknown. **Partition names are not guessed here.** A/B is likely for an Android 15 device, but not confirmed. |
| Recovery arrangement | Unknown. Android 10+ devices often have no separate recovery partition. NovaOS does not assume one. |
| Temporary boot (`fastboot boot`) | Unknown. Many MediaTek bootloaders reject it. It must be tested before any flashing is considered. |
| Stock restoration route | **Not verified.** No official factory-image download or flashing tool for the A001T was found. Without one, flashing anything is unsafe. |

## 3. Sources, documentation and firmware

| Item | Finding |
|---|---|
| Vendor kernel source | **Not found.** [github.com/NothingOSS][nothingoss] shows kernels for Phone (1), (2) and (2a) only. Users have asked for the Phone (3a) source ([community request][req]). Even when released, it will be read only as hardware documentation, not linked into NovaOS. |
| SoC documentation | MediaTek does not publish public reference manuals for the Dimensity 7300. Register-level information would have to come from vendor GPL kernel sources. |
| Firmware | Display, touch, Wi-Fi/BT (likely MediaTek connectivity), modem, GPU, camera and audio DSP firmware are proprietary and loaded at runtime. Redistribution terms are unknown. **None of it is NovaOS code.** |

## 4. Hardware dependencies (what NovaOS would need, in order)

1. **First light:** an ARM64 kernel image the bootloader accepts, an early console (UART pads or the
   USB-ACM gadget — both unknown), the GIC (version unknown, probably GICv3), the ARM generic timer.
2. **Clocks, pin control, regulators (PMIC):** MediaTek-specific; documented only in vendor sources.
3. **Display:** MediaTek DSI + panel driver IC (unknown), or a firmware-left-on framebuffer (`simple-framebuffer`) if the bootloader leaves one; unknown.
4. **Touch:** controller IC unknown (I²C/SPI).
5. **Storage:** UFS 2.2 host controller (MediaTek UFS + UniPro/M-PHY) — a large driver.
6. **USB:** MediaTek MTU3/xHCI.
7. **Power / charging:** PMIC and charger IC (unknown), fuel gauge.
8. **Wi-Fi / Bluetooth:** integrated MediaTek connectivity subsystem with proprietary firmware → **blocked**.
9. **Cellular (4G/5G):** the modem is integrated in the SoC. The host interface is MediaTek's proprietary CCCI/DPMAIF, **not** a standard AT or QMI USB modem. The work splits into separate items: firmware loading, transport, SIM, registration, packet data, SMS, voice, IMS/VoLTE, audio routing. Each is **blocked**: there is no public protocol documentation. NovaOS will never show a simulated connection, and baseband firmware will not be modified.
10. **Audio, sensors, camera, GPU (Mali-G615):** blocked / out of scope until 1–7 work.

## 5. Precise blocker, evidence and next step

- **Blocker:** there is no verified way to run unsigned code on an A001T, and no verified way to restore
  stock firmware. Without both, no write procedure can be offered safely.
- **Evidence:** no official unlock or factory-image documentation was found for this model; the
  NothingOSS GitHub organisation has no 3a-series kernel source; there are no public MediaTek register manuals.
- **Work completed:** the ARM64 port plan (ADR #3, #17) keeps phone-specific code in `ports/` later. This report.
- **Next implementable step (no phone needed):** the ARM64 QEMU `virt` port (M5).
- **Next step on the phone (read-only, owner action):** with the phone in Developer options, record
  `getprop ro.product.device`, `ro.boot.slot_suffix`, `ro.boot.verifiedbootstate` and `ls -l /dev/block/by-name`
  using `adb shell` (reading only, no root needed for most). Check whether OEM unlocking is offered. **Do not unlock or flash
  anything** until Nothing (or a verified source) provides a stock restoration image for A001T.

Rules this project follows for the phone: never overwrite calibration, radio (NVRAM/NVDATA/IMEI), security
or bootloader partitions; never relock the bootloader with a custom image; never present an untested
image as installable. QEMU results say nothing about phone compatibility.

[gsma-news]: https://m.gsmarena.com/nothing_phone_3a_lite_key_specs_europe_price_sale_date-news-70035.php
[gsma]: https://m.gsmarena.com/nothing_phone_(3a)_lite_5g-14267.php
[fonearena]: https://www.fonearena.com/blog/467608/nothing-phone-3a-lite-price-specifications.html
[nothingoss]: https://github.com/NothingOSS
[req]: https://nothing.community/d/29346-kernel-source-release-for-nothing-phone-3apro
