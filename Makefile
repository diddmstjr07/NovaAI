NASM ?= nasm
QEMU ?= qemu-system-x86_64

# Toolchain: prefer a real x86_64-elf cross toolchain (macOS/Homebrew), and
# fall back to a native x86-64 GCC/binutils toolchain where available.
CROSS_PREFIX ?= x86_64-elf-
ifeq ($(shell command -v $(CROSS_PREFIX)gcc 2>/dev/null),)
CC = gcc
LD = ld
OBJCOPY = objcopy
else
CC = $(CROSS_PREFIX)gcc
LD = $(CROSS_PREFIX)ld
OBJCOPY = $(CROSS_PREFIX)objcopy
endif
QEMU_DISPLAY = cocoa,zoom-to-fit=on,full-screen=on

# --- Networking (stage 0) -----------------------------------------------
# QEMU user-mode networking. Guest 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
# No host privileges required; ICMP is limited, so probe connectivity with TCP.
PCAP := net.pcap
QEMU_NET := -netdev user,id=n0 -device e1000,netdev=n0
# Writes every frame crossing netdev n0 to a libpcap file readable by Wireshark.
QEMU_PCAP := -object filter-dump,id=f0,netdev=n0,file=$(PCAP)

BUILD_DIR := build
BUILD_STAMP := $(BUILD_DIR)/.dir
IMAGE := $(BUILD_DIR)/novaos.img
IMAGE_SIZE_MB := 512
CHROME_DEB := downloads/google-chrome-stable_current_amd64.deb
CHROME_STAMP := $(BUILD_DIR)/.chrome-package.sha256

CFLAGS := -std=gnu11 -O2 -g -m64 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
	-Wall -Wextra -Werror -Iinclude

C_SOURCES := \
	kernel/kernel.c \
	kernel/heap.c \
	kernel/filesystem.c \
	kernel/deb.c \
	kernel/vm.c \
	kernel/process.c \
	kernel/network.c \
	kernel/compiler.c \
	kernel/account.c \
	runtime/runtime.c \
	drivers/ata.c \
	drivers/pci.c \
	drivers/e1000.c \
	drivers/input.c \
	drivers/rtc.c \
	drivers/serial.c \
	graphics/font.c \
	graphics/graphics.c \
	ui/ui.c

C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ENTRY_OBJECT := $(BUILD_DIR)/kernel/entry.o
PROCESS_ARCH_OBJECT := $(BUILD_DIR)/kernel/process_arch.o
BUILTIN_OBJECT := $(BUILD_DIR)/user/builtin.o
USER_OBJECT := $(BUILD_DIR)/user/init.o
USER_ELF := $(BUILD_DIR)/user/init.elf
LIBNOVA_OBJECT := $(BUILD_DIR)/user/libnova.o
LIBNOVA_ELF := $(BUILD_DIR)/user/libnova.so
NOVA_LD_OBJECT := $(BUILD_DIR)/user/nova_ld.o
NOVA_LD_ELF := $(BUILD_DIR)/user/nova-ld.so
LIBC_OBJECT := $(BUILD_DIR)/user/libc.o
LIBC_ELF := $(BUILD_DIR)/user/libc.so.6
LIBPTHREAD_OBJECT := $(BUILD_DIR)/user/libpthread.o
LIBPTHREAD_COMPAT_OBJECT := $(BUILD_DIR)/user/pthread_compat.o
LIBPTHREAD_ELF := $(BUILD_DIR)/user/libpthread.so.0
LIBDL_OBJECT := $(BUILD_DIR)/user/libdl.o
LIBDL_ELF := $(BUILD_DIR)/user/libdl.so.2
LIBM_OBJECT := $(BUILD_DIR)/user/libm.o
LIBM_ELF := $(BUILD_DIR)/user/libm.so.6
LIBGCC_OBJECT := $(BUILD_DIR)/user/libgcc_s.o
LIBGCC_ELF := $(BUILD_DIR)/user/libgcc_s.so.1
LIBTLS_OBJECT := $(BUILD_DIR)/user/libtls.o
LIBTLS_ELF := $(BUILD_DIR)/user/libtls.so
LIBTLSDESC_OBJECT := $(BUILD_DIR)/user/libtlsdesc.o
LIBTLSDESC_ELF := $(BUILD_DIR)/user/libtlsdesc.so
LIBCTOR_OBJECT := $(BUILD_DIR)/user/libctor.o
LIBCTOR_ELF := $(BUILD_DIR)/user/libctor.so
TEST_DEB := $(BUILD_DIR)/user/nova-test.deb

.PHONY: all build run debug clean info chrome-image

all build: $(IMAGE)

ifneq ($(wildcard $(CHROME_DEB)),)
all build run: $(CHROME_STAMP)
endif

$(BUILD_STAMP):
	mkdir -p $(BUILD_DIR)
	touch $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ENTRY_OBJECT): kernel/entry.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(PROCESS_ARCH_OBJECT): kernel/process_arch.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(USER_OBJECT): user/init.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(LIBNOVA_OBJECT): user/libnova.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(LIBNOVA_ELF): $(LIBNOVA_OBJECT) user/libnova.map
	$(LD) -m elf_x86_64 -shared -soname libnova.so --hash-style=gnu \
		--version-script=user/libnova.map -nostdlib -o $@ $<

$(NOVA_LD_OBJECT): user/nova_ld.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(NOVA_LD_ELF): $(NOVA_LD_OBJECT)
	$(LD) -m elf_x86_64 -shared -soname nova-ld.so --hash-style=gnu \
		-nostdlib -o $@ $<

$(LIBC_OBJECT): user/libc.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBC_ELF): $(LIBC_OBJECT) user/libc.map
	$(LD) -m elf_x86_64 -shared -soname libc.so.6 --hash-style=gnu \
		--version-script=user/libc.map -nostdlib -o $@ $<

$(LIBPTHREAD_OBJECT): user/libpthread.asm | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(LIBPTHREAD_COMPAT_OBJECT): user/pthread_compat.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBPTHREAD_ELF): $(LIBPTHREAD_OBJECT) $(LIBPTHREAD_COMPAT_OBJECT) user/libpthread.map
	$(LD) -m elf_x86_64 -shared -soname libpthread.so.0 --hash-style=gnu \
		--version-script=user/libpthread.map -nostdlib -o $@ \
		$(LIBPTHREAD_OBJECT) $(LIBPTHREAD_COMPAT_OBJECT)

$(LIBDL_OBJECT): user/libdl.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBDL_ELF): $(LIBDL_OBJECT) user/libdl.map
	$(LD) -m elf_x86_64 -shared -soname libdl.so.2 --hash-style=gnu \
		--version-script=user/libdl.map -nostdlib -o $@ $<

$(LIBM_OBJECT): user/libm.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBM_ELF): $(LIBM_OBJECT) user/libm.map
	$(LD) -m elf_x86_64 -shared -soname libm.so.6 --hash-style=gnu \
		--version-script=user/libm.map -nostdlib -o $@ $<

$(LIBGCC_OBJECT): user/libgcc_s.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -fno-omit-frame-pointer -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBGCC_ELF): $(LIBGCC_OBJECT) user/libgcc_s.map
	$(LD) -m elf_x86_64 -shared -soname libgcc_s.so.1 --hash-style=gnu \
		--version-script=user/libgcc_s.map -nostdlib -o $@ $<

$(LIBTLS_OBJECT): user/libtls.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone \
		-Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBTLS_ELF): $(LIBTLS_OBJECT) user/libtls.map
	$(LD) -m elf_x86_64 -shared -soname libtls.so --hash-style=gnu \
		--version-script=user/libtls.map -nostdlib -o $@ $<

$(LIBTLSDESC_OBJECT): user/libtlsdesc.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -mtls-dialect=gnu2 \
		-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone \
		-Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBTLSDESC_ELF): $(LIBTLSDESC_OBJECT) user/libtlsdesc.map
	$(LD) -m elf_x86_64 -shared -soname libtlsdesc.so --hash-style=gnu \
		--version-script=user/libtlsdesc.map -nostdlib -o $@ $<

$(LIBCTOR_OBJECT): user/libctor.c | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -m64 -ffreestanding -fno-builtin \
		-fno-stack-protector -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables \
		-mno-red-zone -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(LIBCTOR_ELF): $(LIBCTOR_OBJECT) user/libctor.map
	$(LD) -m elf_x86_64 -shared -soname libctor.so --hash-style=gnu \
		--version-script=user/libctor.map -nostdlib -o $@ $<

$(TEST_DEB): tools/make_test_deb.py | $(BUILD_STAMP)
	python3 $< $@

$(USER_ELF): $(USER_OBJECT) $(LIBNOVA_ELF) $(NOVA_LD_ELF) $(LIBC_ELF) \
		$(LIBPTHREAD_ELF) $(LIBM_ELF) $(LIBGCC_ELF) $(LIBTLS_ELF) \
		$(LIBTLSDESC_ELF) $(LIBCTOR_ELF)
	$(LD) -m elf_x86_64 -pie --dynamic-linker /nova-ld.so -z now \
		--hash-style=gnu --allow-shlib-undefined -nostdlib -e _start -L$(BUILD_DIR)/user \
		-rpath / -o $@ $(USER_OBJECT) -l:libnova.so -l:libc.so.6 \
		-l:libpthread.so.0 -l:libm.so.6 -l:libgcc_s.so.1 -l:libtls.so \
		-l:libtlsdesc.so -l:libctor.so

$(BUILTIN_OBJECT): user/builtin.asm $(USER_ELF) $(LIBNOVA_ELF) $(NOVA_LD_ELF) \
		$(LIBC_ELF) $(LIBPTHREAD_ELF) $(LIBDL_ELF) $(LIBM_ELF) $(LIBGCC_ELF) \
		$(LIBTLS_ELF) $(LIBTLSDESC_ELF) $(LIBCTOR_ELF) $(TEST_DEB) | $(BUILD_STAMP)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD_DIR)/kernel.elf: $(ENTRY_OBJECT) $(PROCESS_ARCH_OBJECT) $(BUILTIN_OBJECT) $(C_OBJECTS) kernel/linker.ld
	$(LD) -m elf_x86_64 -T kernel/linker.ld -nostdlib \
		-Map=$(BUILD_DIR)/kernel.map -o $@ $(ENTRY_OBJECT) $(PROCESS_ARCH_OBJECT) \
		$(BUILTIN_OBJECT) $(C_OBJECTS)

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/stage1.bin: boot/stage1.asm | $(BUILD_STAMP)
	$(NASM) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512

$(BUILD_DIR)/stage2.bin: boot/stage2.asm $(BUILD_DIR)/kernel.bin | $(BUILD_STAMP)
	@kernel_sectors=$$((($$(wc -c < $(BUILD_DIR)/kernel.bin) + 511) / 512)); \
		test $$kernel_sectors -le 1024 || \
		(echo "Kernel exceeds loader limit: $$kernel_sectors sectors" >&2; exit 1); \
		$(NASM) -f bin -DKERNEL_SECTORS=$$kernel_sectors $< -o $@
	@test $$(wc -c < $@) -eq 8192

$(IMAGE): $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin
	@if [ ! -f $@ ]; then \
		dd if=/dev/zero of=$@ bs=1048576 count=$(IMAGE_SIZE_MB) 2>/dev/null; \
	elif [ $$(wc -c < $@) -lt $$(( $(IMAGE_SIZE_MB) * 1048576 )) ]; then \
		dd if=/dev/zero of=$@ bs=1048576 count=1 seek=$$(( $(IMAGE_SIZE_MB) - 1 )) conv=notrunc 2>/dev/null; \
	fi
	dd if=$(BUILD_DIR)/stage1.bin of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(BUILD_DIR)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(BUILD_DIR)/kernel.bin of=$@ bs=512 seek=17 conv=notrunc 2>/dev/null
	@echo "Built $(IMAGE) ($$(wc -c < $(BUILD_DIR)/kernel.bin) byte C kernel)"

$(CHROME_STAMP): $(CHROME_DEB) tools/import_deb_to_novafs.py | $(IMAGE)
	@package_hash=$$(shasum -a 256 $(CHROME_DEB) | awk '{print $$1}'); \
	if [ -f $@ ] && grep -qx "$$package_hash" $@; then \
		echo "Chrome package already present in $(IMAGE)"; \
	else \
		python3 tools/import_deb_to_novafs.py $(CHROME_DEB) $(IMAGE); \
		echo "$$package_hash" > $@; \
	fi

chrome-image: $(CHROME_STAMP)

run: $(IMAGE)
	$(QEMU) -machine pc,accel=tcg -cpu qemu64 -m 1024M -vga std \
		-drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) $(QEMU_PCAP) \
		-display $(QEMU_DISPLAY) -serial stdio -monitor none -no-reboot

debug: $(IMAGE)
	$(QEMU) -machine pc,accel=tcg -cpu qemu64 -m 1024M -vga std \
		-drive file=$(IMAGE),format=raw,if=ide -boot order=c \
		$(QEMU_NET) $(QEMU_PCAP) \
		-display $(QEMU_DISPLAY) -serial stdio -monitor none -no-reboot -s -S

info: $(BUILD_DIR)/kernel.elf
	@$(OBJCOPY) --version | head -n 1
	@wc -c $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin 2>/dev/null || true

clean:
	rm -rf -- $(BUILD_DIR)
