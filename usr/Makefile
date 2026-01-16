# Based on xv6-riscv Makefile; see LICENSE.

TOOLPREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(TOOLPREFIX)gcc
AS=$(TOOLPREFIX)as
LD=$(TOOLPREFIX)ld
OBJCOPY=$(TOOLPREFIX)objcopy
OBJDUMP=$(TOOLPREFIX)objdump

UMODE := 1

ULIB_OBJS = \
	uio.o \
	string.o \
	syscall.o \
	heap.o

ULIB_LD = no_umode.ld

ifeq ($(UMODE), 1)
	ULIB_OBJS += start.o
	ULIB_LD = umode.ld
endif

ALL_TARGETS = \
	hello \
	shell \
	cat \
	wc \
	touch \
	rm \
	echo \
	ls

CFLAGS = -Wall -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64g -mabi=lp64d
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -I.

ifeq ($(UMODE), 1)
	CFLAGS += -DUMODE
endif

all: $(addprefix bin/, $(ALL_TARGETS))

bin/%: $(ULIB_OBJS) progs/%.o | bin
	$(LD) -T $(ULIB_LD) -o $@ $^

bin: 
	mkdir $@

clean:
	rm -rf *.o *.elf *.asm bin/
