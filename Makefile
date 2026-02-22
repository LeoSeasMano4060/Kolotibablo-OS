# =============================================================================
# Makefile — Kolotibablo OS
# Uso: make | make iso | make clean | make run
# =============================================================================

CC             = gcc
AS             = nasm
LD             = ld
GRUB_MKRESCUE  = grub-mkrescue
QEMU           = qemu-system-i386

CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -O2
ASFLAGS = -f elf32
LDFLAGS = -T linker.ld -m elf_i386 -nostdlib

OBJECTS = boot.o kernel.o
KERNEL  = kolotibablo.elf
ISO     = kolotibablo.iso
ISODIR  = iso

.PHONY: all iso run clean

all: $(KERNEL)

$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

boot.o: boot.s
	$(AS) $(ASFLAGS) boot.s -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

iso: $(KERNEL)
	mkdir -p $(ISODIR)/boot/grub
	cp $(KERNEL) $(ISODIR)/boot/kernel.elf
	cp grub.cfg  $(ISODIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $(ISO) $(ISODIR)
	@echo ""
	@echo "ISO generada: $(ISO)"

run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 32M

clean:
	rm -f *.o $(KERNEL) $(ISO)
	rm -rf $(ISODIR)

