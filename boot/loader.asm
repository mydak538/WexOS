; loader.asm - minimal placeholder (not required if using GRUB + multiboot kernel)
; This file is kept for compatibility with builds that concat loader + kernel.
BITS 32
org 0x7C00

; Minimal boot sector stub that just hangs.
; If you use GRUB/multiboot, you don't need this file.
start:
    cli
    hlt
times 510-($-$$) db 0
dw 0xAA55
