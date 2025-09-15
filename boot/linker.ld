ENTRY(_start)

SECTIONS {
    . = 0x00100000;   /* загружаем ядро по адресу 1 МБ */

    .text ALIGN(4K) : {
        *(.multiboot)   /* multiboot header */
        *(.text*)       /* код */
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)     /* константы */
    }

    .data ALIGN(4K) : {
        *(.data*)       /* данные */
    }

    .bss ALIGN(4K) : {
        *(COMMON)       /* глобальные переменные */
        *(.bss*)        /* неинициализированные */
    }
}
