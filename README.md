# 🚀 WexOS - Самодельная x86 операционная система

## 📖 О проекте

WexOS - это самодельная операционная система для архитектуры x86, написанная с нуля на C и ассемблере. Система включает ядро, файловую систему, командную оболочку с 30+ командами и различные утилиты.

## ✨ Возможности

- **Монолитное ядро** с поддержкой многозадачности
- **FAT32 файловая система** с персистентным хранением
- **Командная оболочка** с 30+ встроенными командами
- **Драйверы**: VGA, клавиатура, ATA диски, RTC
- **Утилиты**: Текстовый редактор, калькулятор, просмотрщик памяти
- ![Writer WexOS](screen/1.png)

## 🛠️ Сборка и запуск

### Требования
- GCC с поддержкой cross-compilation
- GRUB (загрузчик)
- QEMU или VirtualBox
  
### Сборка
# 1. Компиляция ядра (32-bit, freestanding)
gcc -m32 -ffreestanding -fno-pie -O2 -c kernel/kernel.c -o bin/kernel.o

# 2. Линковка ядра с явной точкой входа (_start)
ld -m elf_i386 -T boot/linker.ld -o bin/kernel.bin bin/kernel.o -e _start

# 3. Копируем ядро в ISO
cp bin/kernel.bin iso/boot/

# 4. Копируем SystemRoot в ISO
cp -r systemroot iso/SystemRoot

# 6. Собираем ISO с помощью grub-mkrescue
grub-mkrescue -o bin/wexos.iso iso
![Memory WexOS](screen/2.png)


