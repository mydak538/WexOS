# --- Directories ---
BIN_DIR = bin
ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
SYSTEMROOT_DIR = $(ISO_DIR)/SystemRoot

# --- Targets ---
KERNEL = $(BIN_DIR)/kernel.bin
RECOVERY = $(BIN_DIR)/recovery.bin
INSTALLER = $(BIN_DIR)/install.bin
ISO_IMAGE = $(BIN_DIR)/wexos.iso

# --- Compiler and Linker flags ---
CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -fno-pie -O2
LDFLAGS = -m elf_i386 -T boot/linker.ld

# --- Default target ---
all: $(ISO_IMAGE)

# --- Kernel ---
$(BIN_DIR)/kernel.o: kernel/kernel.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c kernel/kernel.c -o $(BIN_DIR)/kernel.o

$(KERNEL): $(BIN_DIR)/kernel.o boot/linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) $(BIN_DIR)/kernel.o -e _start

$(BOOT_DIR)/kernel.bin: $(KERNEL)
	@mkdir -p $(BOOT_DIR)
	cp $(KERNEL) $(BOOT_DIR)/

# --- Recovery ---
$(BIN_DIR)/recovery.o: kernel/recovery.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c kernel/recovery.c -o $(BIN_DIR)/recovery.o

$(RECOVERY): $(BIN_DIR)/recovery.o boot/linker.ld
	$(LD) $(LDFLAGS) -o $(RECOVERY) $(BIN_DIR)/recovery.o -e _start

$(BOOT_DIR)/recovery.bin: $(RECOVERY)
	@mkdir -p $(BOOT_DIR)
	cp $(RECOVERY) $(BOOT_DIR)/

# --- Installer ---
$(BIN_DIR)/install.o: kernel/install.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c kernel/install.c -o $(BIN_DIR)/install.o

$(INSTALLER): $(BIN_DIR)/install.o boot/linker.ld
	$(LD) $(LDFLAGS) -o $(INSTALLER) $(BIN_DIR)/install.o -e _start

$(BOOT_DIR)/install.bin: $(INSTALLER)
	@mkdir -p $(BOOT_DIR)
	cp $(INSTALLER) $(BOOT_DIR)/

# --- SystemRoot ---
$(SYSTEMROOT_DIR): systemroot
	@mkdir -p $(SYSTEMROOT_DIR)
	cp -r systemroot/* $(SYSTEMROOT_DIR)/

# --- ISO build ---
$(ISO_IMAGE): $(BOOT_DIR)/kernel.bin $(BOOT_DIR)/recovery.bin $(BOOT_DIR)/install.bin $(SYSTEMROOT_DIR)
	grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR)

# --- Shortcut targets ---
iso: $(ISO_IMAGE)

kernel: $(KERNEL)

recovery: $(RECOVERY)

installer: $(INSTALLER)

systemroot: $(SYSTEMROOT_DIR)

# --- Clean targets ---
clean:
	rm -rf $(BIN_DIR)/*.o $(BIN_DIR)/*.bin

clean-iso:
	rm -f $(ISO_IMAGE)

clean-all:
	rm -rf $(BIN_DIR) $(ISO_DIR)

# --- Utility targets ---
distclean: clean-all

mrproper: clean-all

.PHONY: all iso kernel recovery installer systemroot clean clean-iso clean-all distclean mrproper
