# NovaOS — own kernel, native shell and Nova Recovery.
#   make doctor | kernel | image | run | recovery | test | package | theme | clean
# The previous Debian-based appliance is kept, unmaintained, in legacy.mk (make -f legacy.mk ...).
ARCH   ?= x86_64
TARGET ?= qemu-$(ARCH)
VERSION := 0.4.0
BUILD  := build/$(ARCH)
OUT    := out
ISO    := $(OUT)/novaos-$(VERSION)-$(TARGET).iso
RISO   := $(OUT)/novaos-$(VERSION)-$(TARGET)-recovery-default.iso

ifeq ($(filter $(ARCH),x86_64 aarch64),)
$(error ARCH=$(ARCH) is not supported. Use x86_64 or aarch64)
endif
ifneq ($(TARGET),qemu-$(ARCH))
$(error TARGET=$(TARGET) does not match ARCH=$(ARCH). Supported: qemu-x86_64 (ARCH=x86_64), qemu-aarch64 (ARCH=aarch64). Phone: see docs/PHONE_PORT.md)
endif

CC  := clang
LD  := ld.lld
CFLAGS_COMMON := --target=$(ARCH)-unknown-none-elf -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding \
  -fno-stack-protector -fno-pic -fno-pie -mgeneral-regs-only -fno-asynchronous-unwind-tables \
  -fno-omit-frame-pointer -ffile-prefix-map=$(CURDIR)=. -Iinclude -Ilibs/gfx -Itheme/generated
ifeq ($(ARCH),x86_64)
CFLAGS_COMMON += -mno-red-zone
USER_BASE := 0x40000000
KSRC_ARCH := arch/x86_64/start.c arch/x86_64/cpu.c arch/x86_64/vmm.c drivers/pc/serial.c drivers/pc/pic_pit.c \
             drivers/pc/ps2kbd.c drivers/pc/rtc_pci_power.c drivers/pc/virtio_pci.c
KASM := arch/x86_64/boot.S arch/x86_64/isr.S
else
CFLAGS_COMMON += -mstrict-align
USER_BASE := 0x8000000000
KSRC_ARCH := arch/aarch64/start.c arch/aarch64/vmm.c drivers/virt/pl011.c drivers/virt/gic_timer.c \
             drivers/virt/fw_cfg_ramfb.c drivers/virt/virtio_mmio.c drivers/virt/rtc_psci.c
KASM := arch/aarch64/boot.S arch/aarch64/vectors.S
endif
KCFLAGS := $(CFLAGS_COMMON) -Ikernel -Iarch/$(ARCH) -D__NOVA_KERNEL__
UCFLAGS := $(CFLAGS_COMMON) -Ilibs/libnova -Ilibs/novaui

KSRC := kernel/main.c kernel/lib.c kernel/pmm.c kernel/task.c kernel/syscall.c kernel/elf.c kernel/input.c kernel/store.c kernel/virtio_blk.c \
        kernel/panic_screen.c libs/gfx/gfx.c $(KSRC_ARCH)
KOBJ := $(patsubst %,$(BUILD)/k/%.o,$(KSRC) $(KASM)) $(BUILD)/k/programs.o $(BUILD)/k/blobs.o
LIBNOVA := $(BUILD)/u/libs/libnova/crt0.S.o $(BUILD)/u/libs/libnova/nova.c.o
LIBUI   := $(BUILD)/u/libs/novaui/novaui.c.o $(BUILD)/u/libs/gfx/gfx.c.o $(BUILD)/u/blobs.o
PROGS := init shell recovery probe
HDRS := $(wildcard include/nova/*.h kernel/*.h arch/$(ARCH)/*.h libs/*/*.h) theme/generated/assets.h

.PHONY: all doctor kernel image run recovery test package theme clean
all: image

doctor:
	@tools/nova-doctor.sh

$(BUILD)/k/%.o: % $(HDRS)
	@mkdir -p $(dir $@)
	@echo "  CC   $<"; $(CC) $(KCFLAGS) -c $< -o $@
$(BUILD)/u/%.o: % $(HDRS)
	@mkdir -p $(dir $@)
	@echo "  CC   $<"; $(CC) $(UCFLAGS) -c $< -o $@
$(BUILD)/k/blobs.o $(BUILD)/u/blobs.o: theme/generated/blobs.S $(wildcard theme/generated/*.bin)
	@mkdir -p $(dir $@)
	$(CC) --target=$(ARCH)-unknown-none-elf -Wa,-Itheme/generated -c $< -o $@

$(BUILD)/user/init.elf: $(BUILD)/u/services/init/main.c.o $(LIBNOVA)
$(BUILD)/user/probe.elf: $(BUILD)/u/tests/kernel/probe.c.o $(LIBNOVA)
$(BUILD)/user/shell.elf: $(BUILD)/u/shell/main.c.o $(LIBUI) $(LIBNOVA)
$(BUILD)/user/recovery.elf: $(BUILD)/u/recovery/main.c.o $(LIBUI) $(LIBNOVA)
$(BUILD)/user.ld: libs/libnova/user.ld Makefile
	@mkdir -p $(dir $@)
	sed 's/@USER_BASE@/$(USER_BASE)/' $< > $@
$(BUILD)/user/%.elf: $(BUILD)/user.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -static --build-id=none -z max-page-size=4096 -T $(BUILD)/user.ld -o $@ $(filter %.o,$^)

$(BUILD)/k/programs.o: kernel/programs.S $(patsubst %,$(BUILD)/user/%.elf,$(PROGS))
	$(CC) --target=$(ARCH)-unknown-none-elf -Wa,-I$(BUILD)/user -c $< -o $@

$(BUILD)/novaos.elf: $(KOBJ) arch/$(ARCH)/kernel.ld
	$(LD) -nostdlib -static --build-id=none -z max-page-size=4096 -T arch/$(ARCH)/kernel.ld -o $@ $(KOBJ)

kernel: $(BUILD)/novaos.elf

define mkiso
	@mkdir -p $(BUILD)/iso-$(2)/boot/grub $(OUT)
	cp $(BUILD)/novaos.elf $(BUILD)/iso-$(2)/boot/novaos.elf
	@mkdir -p $(BUILD)/iso-$(2)/licenses && cp licenses/* THIRD_PARTY_LICENSES $(BUILD)/iso-$(2)/licenses/
	sed -e 's/@TIMEOUT@/$(3)/' -e 's/@DEFAULT@/$(2)/' boot/grub/grub.cfg.in > $(BUILD)/iso-$(2)/boot/grub/grub.cfg
	SOURCE_DATE_EPOCH=$$(git log -1 --format=%ct 2>/dev/null || echo 0) \
	  grub-mkrescue -o $(1) $(BUILD)/iso-$(2) -- -volid NOVAOS 2>$(BUILD)/grub-mkrescue-$(2).log \
	  || { cat $(BUILD)/grub-mkrescue-$(2).log; exit 1; }
	cd $(OUT) && sha256sum $(notdir $(1)) > $(notdir $(1)).sha256
endef

$(ISO): $(BUILD)/novaos.elf boot/grub/grub.cfg.in
	$(call mkiso,$@,0,3)
$(RISO): $(BUILD)/novaos.elf boot/grub/grub.cfg.in
	$(call mkiso,$@,1,1)

ifeq ($(ARCH),x86_64)
image: $(ISO)
	@ls -l $(ISO) && cat $(ISO).sha256

# The data disk (notes, tasks, settings) persists between runs in out/data-<arch>.img.
DISK := $(OUT)/data-$(ARCH).img
$(DISK):
	@mkdir -p $(OUT) && truncate -s 1M $@
QEMU := qemu-system-x86_64 -m 512 -serial stdio -no-reboot -device VGA \
        -drive file=$(DISK),if=none,id=d0,format=raw -device virtio-blk-pci,drive=d0,disable-modern=on
run: $(ISO) $(DISK)
	$(QEMU) -cdrom $(ISO)
run-uefi: $(ISO) $(DISK)
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -cdrom $(ISO)
recovery: $(RISO) $(DISK)
	$(QEMU) -cdrom $(RISO)
test: $(ISO) $(RISO)
	python3 tools/nova-test.py --arch x86_64 --iso $(ISO) --recovery-iso $(RISO) --out $(OUT)/test-x86_64
package: image $(RISO)
	tools/nova-package.sh $(VERSION) $(TARGET) $(ISO) $(RISO)
else
# QEMU virt loads the kernel ELF directly (-kernel); the boot mode comes from fw_cfg (opt/nova/cmdline).
KIMG := $(OUT)/novaos-$(VERSION)-$(TARGET).elf
$(KIMG): $(BUILD)/novaos.elf
	@mkdir -p $(OUT)
	cp $< $@
	cd $(OUT) && sha256sum $(notdir $@) > $(notdir $@).sha256
image: $(KIMG)
	@ls -l $(KIMG) && cat $(KIMG).sha256
DISK := $(OUT)/data-$(ARCH).img
$(DISK):
	@mkdir -p $(OUT) && truncate -s 1M $@
QEMU := qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -serial stdio -no-reboot \
        -device ramfb -device virtio-keyboard-device -device virtio-tablet-device -global virtio-mmio.force-legacy=false \
        -drive file=$(DISK),if=none,id=d0,format=raw -device virtio-blk-device,drive=d0
run: $(KIMG) $(DISK)
	$(QEMU) -kernel $(KIMG)
recovery: $(KIMG) $(DISK)
	$(QEMU) -kernel $(KIMG) -fw_cfg name=opt/nova/cmdline,string=nova.recovery
test: $(KIMG)
	python3 tools/nova-test.py --arch aarch64 --kernel $(KIMG) --out $(OUT)/test-aarch64
package: image
	tools/nova-package.sh $(VERSION) $(TARGET) $(KIMG)
endif

# Regenerate theme/generated from theme/tokens.json (needs the legacy UI build: make -f legacy.mk ui).
theme:
	node theme/tools/gen-assets.mjs

clean:
	rm -rf build/$(ARCH) $(OUT)/novaos-$(VERSION)-$(TARGET)* $(OUT)/test-$(ARCH)
