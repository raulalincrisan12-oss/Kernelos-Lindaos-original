CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T src/linker.ld

OBJS = boot.o kernel.o gdt.o idt.o input.o usb.o vblank.o ata.o

all: myos.iso

boot.o: src/boot.asm
	$(AS) $(ASFLAGS) src/boot.asm -o boot.o

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c src/kernel.c -o kernel.o

gdt.o: src/gdt.c
	$(CC) $(CFLAGS) -c src/gdt.c -o gdt.o

idt.o: src/idt.c
	$(CC) $(CFLAGS) -c src/idt.c -o idt.o

input.o: src/input.c
	$(CC) $(CFLAGS) -c src/input.c -o input.o

usb.o: src/usb.c
	$(CC) $(CFLAGS) -c src/usb.c -o usb.o

vblank.o: src/vblank.c
	$(CC) $(CFLAGS) -c src/vblank.c -o vblank.o

ata.o: src/ata.c
	$(CC) $(CFLAGS) -c src/ata.c -o ata.o

myos.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o myos.bin $(OBJS)

myos.iso: myos.bin
	cp myos.bin isodir/boot/myos.bin
	grub-mkrescue -o myos.iso isodir

clean:
	rm -f *.o myos.bin myos.iso isodir/boot/myos.bin
