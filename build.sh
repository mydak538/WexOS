#!/bin/bash
set -e

# --- Configuration ---
BUILD_DIR="bin"
ISO_DIR="iso"
BOOT_DIR="${ISO_DIR}/boot"
SYSTEMROOT_DIR="${ISO_DIR}/SystemRoot"
SOURCE_DIR="systemroot"
ISO_NAME="wexos.iso"
OUTPUT_ISO="${BUILD_DIR}/${ISO_NAME}"

# --- Compiler settings ---
CC="gcc"
LD="ld"
CFLAGS="-m32 -ffreestanding -fno-pie -O2"
LDFLAGS="-m elf_i386 -T boot/linker.ld"

# --- Color codes for output ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Functions ---
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    local deps=("gcc" "ld" "grub-mkrescue")
    local missing=()
    
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            missing+=("$dep")
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing[*]}"
        exit 1
    fi
}

create_directories() {
    print_info "Creating directories..."
    mkdir -p "${BUILD_DIR}" "${BOOT_DIR}"
}

compile_kernel() {
    print_info "Compiling kernel..."
    "${CC}" ${CFLAGS} -c kernel/kernel.c -o "${BUILD_DIR}/kernel.o"
    "${LD}" ${LDFLAGS} -o "${BUILD_DIR}/kernel.bin" "${BUILD_DIR}/kernel.o" -e _start
    cp "${BUILD_DIR}/kernel.bin" "${BOOT_DIR}/"
}

compile_recovery() {
    print_info "Compiling recovery..."
    "${CC}" ${CFLAGS} -c kernel/recovery.c -o "${BUILD_DIR}/recovery.o"
    "${LD}" ${LDFLAGS} -o "${BUILD_DIR}/recovery.bin" "${BUILD_DIR}/recovery.o" -e _start
    cp "${BUILD_DIR}/recovery.bin" "${BOOT_DIR}/"
}

compile_installer() {
    print_info "Compiling installer..."
    "${CC}" ${CFLAGS} -c kernel/install.c -o "${BUILD_DIR}/install.o"
    "${LD}" ${LDFLAGS} -o "${BUILD_DIR}/install.bin" "${BUILD_DIR}/install.o" -e _start
    cp "${BUILD_DIR}/install.bin" "${BOOT_DIR}/"
}

copy_systemroot() {
    print_info "Copying SystemRoot..."
    if [ -d "${SOURCE_DIR}" ]; then
        cp -r "${SOURCE_DIR}" "${SYSTEMROOT_DIR}"
    else
        print_warning "SystemRoot directory not found, skipping..."
    fi
}

build_iso() {
    print_info "Building ISO image..."
    if grub-mkrescue -o "${OUTPUT_ISO}" "${ISO_DIR}"; then
        print_success "ISO built successfully: ${OUTPUT_ISO}"
    else
        print_error "Failed to build ISO"
        exit 1
    fi
}

cleanup() {
    print_info "Cleaning up object files..."
    rm -f "${BUILD_DIR}"/*.o
}

show_build_info() {
    echo "=========================================="
    print_success "Build completed!"
    echo "Output: ${OUTPUT_ISO}"
    echo "Size: $(du -h "${OUTPUT_ISO}" | cut -f1)"
    echo "=========================================="
}

# --- Main execution ---
main() {
    print_info "Starting WexOS build process..."
    
    check_dependencies
    create_directories
    compile_kernel
    compile_recovery
    compile_installer
    copy_systemroot
    build_iso
    cleanup
    show_build_info
}

# --- Handle command line arguments ---
case "${1:-}" in
    "clean")
        print_info "Cleaning build directories..."
        rm -rf "${BUILD_DIR}" "${ISO_DIR}"
        print_success "Clean completed"
        ;;
    "help"|"-h"|"--help")
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  build    Build the project (default)"
        echo "  clean    Clean build directories"
        echo "  help     Show this help message"
        ;;
    "build"|"")
        main
        ;;
    *)
        print_error "Unknown command: $1"
        echo "Use '$0 help' for usage information"
        exit 1
        ;;
esac
