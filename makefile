all: iso

# --- Kernel ---
bin/kernel.o: kernel/kernel.c
	gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/kernel.c -o bin/kernel.o

bin/kernel.bin: bin/kernel.o boot/linker.ld
	ld -m elf_i386 -T boot/linker.ld -o bin/kernel.bin bin/kernel.o -e _start

iso/boot/kernel.bin: bin/kernel.bin
	cp bin/kernel.bin iso/boot/

# --- Recovery ---
bin/recovery.o: kernel/recovery.c
	gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/recovery.c -o bin/recovery.o

bin/recovery.bin: bin/recovery.o boot/linker.ld
	ld -m elf_i386 -T boot/linker.ld -o bin/recovery.bin bin/recovery.o -e _start

iso/boot/recovery.bin: bin/recovery.bin
	cp bin/recovery.bin iso/boot/

# --- Installer ---
bin/install.o: kernel/install.c
	gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/install.c -o bin/install.o

bin/install.bin: bin/install.o boot/linker.ld
	ld -m elf_i386 -T boot/linker.ld -o bin/install.bin bin/install.o -e _start

iso/boot/install.bin: bin/install.bin
	cp bin/install.bin iso/boot/

# --- SystemRoot ---
iso/SystemRoot: systemroot
	cp -r systemroot iso/SystemRoot

# --- ISO build ---
iso: iso/boot/kernel.bin iso/boot/recovery.bin iso/boot/install.bin iso/SystemRoot
	grub-mkrescue -o bin/wexos.iso iso
