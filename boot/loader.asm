BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Очистка экрана
    mov ax, 0x0003
    int 0x10

    mov si, msg_loading
    call print_string

    ; Загружаем (8 секторов)
    mov ax, 0x0208
    mov cx, 0x0002
    mov dh, 0x00
    mov dl, 0x80
    mov bx, 0x7E00
    int 0x13
    jc disk_error

    jmp 0x0000:0x7E00

disk_error:
    mov si, msg_error
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

msg_loading db "Loading WexOS...", 0
msg_error db " Error!", 0

times 510-($-$$) db 0
dw 0xAA55
