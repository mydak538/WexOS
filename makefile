all: iso

# 1. Компиляция ядра (32-bit, freestanding)
bin/kernel.o: kernel/kernel.c
	gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/kernel.c -o bin/kernel.o

# 2. Линковка ядра с явной точкой входа (_start)
bin/kernel.bin: bin/kernel.o boot/linker.ld
	ld -m elf_i386 -T boot/linker.ld -o bin/kernel.bin bin/kernel.o -e _start

# 3. Копируем ядро в ISO
iso/boot/kernel.bin: bin/kernel.bin
	cp bin/kernel.bin iso/boot/

# 4. Копируем SystemRoot в ISO
iso/SystemRoot: systemroot
	cp -r systemroot iso/SystemRoot

# 5. Собираем ISO с помощью grub-mkrescue
iso: iso/boot/kernel.bin iso/SystemRoot
	grub-mkrescue -d /usr/lib/grub/i386-pc -o wexos.iso iso

