#!/bin/bash

set -e

# создаём папки
mkdir -p bin iso/boot

# --- компиляция ядра ---
gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/kernel.c -o bin/kernel.o
ld  -m elf_i386 -T boot/linker.ld -o bin/kernel.bin bin/kernel.o -e _start
cp bin/kernel.bin iso/boot/

# --- компиляция recovery ---
gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/recovery.c -o bin/recovery.o
ld  -m elf_i386 -T boot/linker.ld -o bin/recovery.bin bin/recovery.o -e _start
cp bin/recovery.bin iso/boot/

# --- копирование SystemRoot ---
cp -r systemroot iso/SystemRoot

# --- сборка ISO ---
grub-mkrescue -o bin/wexos.iso iso

echo "Сборка завершена! Файл: bin/wexos.iso"
