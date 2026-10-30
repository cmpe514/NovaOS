# NovaOS — own kernel, native shell and Nova Recovery.
#   make doctor | kernel | image | run | recovery | test | package | theme | clean
# The previous Debian-based appliance is kept, unmaintained, in legacy.mk (make -f legacy.mk ...).
ARCH   ?= x86_64
TARGET ?= qemu-$(ARCH)
VERSION := 0.2.0-m1
BUILD  := build/$(ARCH)
OUT    := out
ISO    := $(OUT)/novaos-$(VERSION)-$(TARGET).iso
RISO   := $(OUT)/novaos-$(VERSION)-$(TARGET)-recovery-default.iso

ifneq ($(ARCH),x86_64)
$(error ARCH=$(ARCH) is not implemented yet. The ARM64 port is milestone M5; see docs/ROADMAP.md)
endif
ifneq ($(TARGET),qemu-x86_64)
$(error TARGET=$(TARGET) is not implemented yet. Supported: qemu-x86_64)
endif

CC  := clang
LD  := ld.lld
CFLAGS_COMMON := --target=x86_64-unknown-none-elf -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding \
  -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only -fno-asynchronous-unwind-tables \
  -fno-omit-frame-pointer -ffile-prefix-map=$(CURDIR)=. -Iinclude -Ilibs/gfx -Itheme/generated
KCFLAGS := $(CFLAGS_COMMON) -Ikernel -Iarch/x86_64 -mcmodel=kernel -D__NOVA_KERNEL__
KCFLAGS := $(filter-out -mcmodel=kernel,$(KCFLAGS))
UCFLAGS := $(CFLAGS_COMMON) -Ilibs/libnova -Ilibs/novaui

KSRC := kernel/main.c kernel/lib.c kernel/pmm.c kernel/vmm.c kernel/task.c kernel/syscall.c kernel/elf.c \
        kernel/panic_screen.c arch/x86_64/cpu.c drivers/pc/serial.c drivers/pc/pic_pit.c drivers/pc/ps2kbd.c \
        drivers/pc/rtc_pci_power.c libs/gfx/gfx.c
KASM := arch/x86_64/boot.S arch/x86_64/isr.S
KOBJ := $(patsubst %,$(BUILD)/k/%.o,$(KSRC) $(KASM)) $(BUILD)/k/programs.o $(BUILD)/k/blobs.o
LIBNOVA := $(BUILD)/u/libs/libnova/crt0.S.o $(BUILD)/u/libs/libnova/nova.c.o
LIBUI   := $(BUILD)/u/libs/novaui/novaui.c.o $(BUILD)/u/libs/gfx/gfx.c.o $(BUILD)/u/blobs.o
PROGS := init shell recovery probe
HDRS := $(wildcard include/nova/*.h kernel/*.h arch/x86_64/*.h libs/*/*.h) theme/generated/assets.h

.PHONY: all doctor kernel image run recovery test package theme clean
all: image

doctor:
	@tools/nova-doctor.sh

$(BUILD)/k/%.o: % $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@
$(BUILD)/u/%.o: % $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@
$(BUILD)/k/blobs.o $(BUILD)/u/blobs.o: theme/generated/blobs.S $(wildcard theme/generated/*.bin)
	@mkdir -p $(dir $@)
	$(CC) --target=x86_64-unknown-none-elf -Wa,-Itheme/generated -c $< -o $@

$(BUILD)/user/init.elf: $(BUILD)/u/services/init/main.c.o $(LIBNOVA)
$(BUILD)/user/probe.elf: $(BUILD)/u/tests/kernel/probe.c.o $(LIBNOVA)
$(BUILD)/user/shell.elf: $(BUILD)/u/shell/main.c.o $(LIBUI) $(LIBNOVA)
$(BUILD)/user/recovery.elf: $(BUILD)/u/recovery/main.c.o $(LIBUI) $(LIBNOVA)
$(BUILD)/user/%.elf: libs/libnova/user.ld
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -static --build-id=none -T libs/libnova/user.ld -o $@ $(filter %.o,$^)

$(BUILD)/k/programs.o: kernel/programs.S $(patsubst %,$(BUILD)/user/%.elf,$(PROGS))
	$(CC) --target=x86_64-unknown-none-elf -Wa,-I$(BUILD)/user -c $< -o $@

$(BUILD)/novaos.elf: $(KOBJ) kernel/kernel.ld
	$(LD) -nostdlib -static --build-id=none -z max-page-size=4096 -T kernel/kernel.ld -o $@ $(KOBJ)

kernel: $(BUILD)/novaos.elf

define mkiso
	@mkdir -p $(BUILD)/iso-$(2)/boot/grub $(OUT)
	cp $(BUILD)/novaos.elf $(BUILD)/iso-$(2)/boot/novaos.elf
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

image: $(ISO)
	@ls -l $(ISO) && cat $(ISO).sha256

QEMU := qemu-system-x86_64 -m 512 -serial stdio -no-reboot -device VGA
run: $(ISO)
	$(QEMU) -cdrom $(ISO)
run-uefi: $(ISO)
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -cdrom $(ISO)
recovery: $(RISO)
	$(QEMU) -cdrom $(RISO)

test: $(ISO) $(RISO)
	python3 tools/nova-test.py --iso $(ISO) --recovery-iso $(RISO) --out $(OUT)/test

package: image $(RISO)
	tools/nova-package.sh $(VERSION) $(TARGET)

# Regenerate theme/generated from theme/tokens.json (needs the legacy UI build: make -f legacy.mk ui).
theme:
	node theme/tools/gen-assets.mjs

clean:
	rm -rf build/x86_64 $(OUT)/novaos-$(VERSION)-* $(OUT)/test
