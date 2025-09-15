#!/bin/bash

# создаём папки
mkdir -p bin iso/boot

# компиляция ядра
gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/kernel.c -o bin/kernel.o

# линковка
ld -m elf_i386 -T boot/linker.ld -o bin/kernel.bin bin/kernel.o -e _start

# копирование
cp bin/kernel.bin iso/boot/
cp -r systemroot iso/SystemRoot

# сборка ISO
grub-mkrescue -o bin/wexos.iso iso

echo "Сборка завершена!"
