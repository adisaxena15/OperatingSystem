# Makefile
# 

PREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(PREFIX)gcc
AS=$(PREFIX)as
LD=$(PREFIX)ld
OBJCOPY=$(PREFIX)objcopy
OBJDUMP=$(PREFIX)objdump

OBJS = \
	elf.o \
	see.o \
	uio.o \
	misc.o \
	ktfs.o \
	intr.o \
	excp.o \
	plic.o \
	trap.o \
	start.o \
	heap0.o \
	error.o \
	timer.o \
	cache.o \
	string.o \
	thrasm.o \
	thread.o \
	device.o \
	filesys.o \
	console.o \
	dev/rtc.o \
	dev/uart.o \
	dev/virtio.o \
	dev/viorng.o \
	dev/viorng.o \
	dev/vioblk.o \
	dev/ramdisk.o \


CFLAGS = -Wall -Werror=implicit-function-declaration
CFLAGS += -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding 
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.

# CFLAGS += -DDEBUG -DTRACE # Everything!
# CFLAGS += -DMEMORY_DEBUG -DMEMORY_TRACE
# CFLAGS += -DHEAP_DEBUG -DHEAP_TRACE
# CFLAGS += -DEZFS_DEBUG -DEZFS_TRACE
# CFLAGS += -DLOCK_DEBUG -DLOCK_TRACE
# CFLAGS += -DPROCESS_DEBUG -DPROCESS_TRACE
# CFLAGS += -DMAIN_DEBUG -DMAIN_TRACE
# CFLAGS += -DSYSCALL_DEBUG -DSYSCALL_TRACE
# CFLAGS += -DUIO_DEBUG -DUIO_TRACE
# CFLAGS += -DTHREAD_DEBUG -DTHREAD_TRACE
# CFLAGS += -DTIMER_DEBUG -DTIMER_TRACE
# CFLAGS += -DVIOGPU_DEBUG -DVIOGPU_TRACE
# CFLAGS += -DCACHE_DEBUG -DCACHE_TRACE
# CFLAGS += -DKTFS_DEBUG -DKTFS_TRACE
# CFLAGS += -DELF_DEBUG -DELF_TRACE

ASFLAGS = -march=rv64imazicsr
LDFLAGS = -melf64lriscv

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -bios none -nographic

QEMUOPTS += -serial mon:stdio
QEMUOPTS += -serial pty
# Add more UARTs

QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0
# Add more RNG devices

QEMUOPTS += -drive file=ktfs.raw,id=blk0,if=none,format=raw,readonly=false
QEMUOPTS += -device virtio-blk-device,drive=blk0

###########################################################################

all: kernel.elf

BLOB_OBJCOPY_FLAGS = \
	--add-section .rodata.blob=blob.raw \
	--set-section-flags .rodata.blob=alloc,contents,load,readonly

blob.o:
	echo .end | $(AS) $(ASFLAGS) -o blob.o
	[ ! -f blob.raw ] || $(OBJCOPY) $(BLOB_OBJCOPY_FLAGS) $@

clean:
	rm -rf *.o dev/*.o tests/*.o demo/*.o *.elf

###################################MAIN.C###################################

kernel.elf: $(OBJS) main.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

################################TEST_MAIN.C#################################

# Add more test objects or test suites
TEST_SUITE_OBJS = tests/testsuite_1.o
TEST_OBJS = $(TEST_SUITE_OBJS) $(OBJS)

test-kernel.elf: $(TEST_OBJS) tests/test_main.o blob.o
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run-test: test-kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test: test-kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

###########################################################################
