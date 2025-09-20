#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_FLAGS 0

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define NULL ((void*)0)
#define SECTOR_SIZE 512
#define FS_SECTOR_START 1
#define MAX_FILES 64

/* Структура файловой системы */
typedef struct { 
    char name[256];
    int is_dir; 
    char content[256]; 
    u32 next_sector;
    u32 size;
} FSNode;

/* Function prototypes */
void putchar(char ch);
char keyboard_getchar();
void memcpy(void* dst, void* src, int len);
int strcmp(const char* a, const char* b);
int strlen(const char* s);
void strcpy(char* dst, const char* src);
char* strcat(char* dest, const char* src);
void prints(const char* s);
void newline();
void itoa(int value, char* str, int base);
void clear_screen();
void delay(int seconds);

/* VGA text buffer */
volatile unsigned short* VGA = (unsigned short*)0xB8000;
enum { ROWS=25, COLS=80 };
static unsigned int cursor_row=0, cursor_col=0;
static unsigned char text_color=0x07;

/* I/O ports */
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline u16 inw(unsigned short port) {
    u16 r;
    __asm__ volatile("inw %1,%0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(unsigned short port, u16 val) {
    __asm__ volatile("outw %0,%1" : : "a"(val), "Nd"(port));
}

/* ATA Disk I/O */
#define ATA_DATA 0x1F0
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DEVICE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_CMD 0x1F7

FSNode fs_cache[MAX_FILES];
int fs_count = 0;
int fs_dirty = 0;

/* Multiboot header */
typedef struct {
    u32 magic;
    u32 flags;
    u32 checksum;
} __attribute__((packed)) multiboot_header_t;

multiboot_header_t multiboot_header __attribute__((section(".multiboot"))) = {
    MULTIBOOT_MAGIC,
    MULTIBOOT_FLAGS,
    -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)
};

/* ATA functions */
void ata_wait_ready() {
    while (inb(ATA_STATUS) & 0x80);
}

void ata_wait_drq() {
    while (!(inb(ATA_STATUS) & 0x08));
}

void ata_read_sector(u32 lba, u8* buffer) {
    outb(ATA_DEVICE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (u8)lba);
    outb(ATA_LBA_MID, (u8)(lba >> 8));
    outb(ATA_LBA_HIGH, (u8)(lba >> 16));
    outb(ATA_CMD, 0x20);

    ata_wait_ready();
    if (inb(ATA_STATUS) & 0x01) {
        prints("ATA Read Error\n");
        return;
    }

    ata_wait_drq();
    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        u16 data = inw(ATA_DATA);
        buffer[i * 2] = (u8)data;
        buffer[i * 2 + 1] = (u8)(data >> 8);
    }
}

void ata_write_sector(u32 lba, u8* buffer) {
    outb(ATA_DEVICE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (u8)lba);
    outb(ATA_LBA_MID, (u8)(lba >> 8));
    outb(ATA_LBA_HIGH, (u8)(lba >> 16));
    outb(ATA_CMD, 0x30);

    ata_wait_ready();
    ata_wait_drq();

    for (int i = 0; i < SECTOR_SIZE / 2; i++) {
        u16 data = (buffer[i * 2 + 1] << 8) | buffer[i * 2];
        outw(ATA_DATA, data);
    }

    ata_wait_ready();
    if (inb(ATA_STATUS) & 0x01) {
        prints("ATA Write Error\n");
    }
}

/* Filesystem functions */
void fs_save_to_disk() {
    if (!fs_dirty) return;

    u8 sector_buffer[SECTOR_SIZE];
    u32 sector = FS_SECTOR_START;

    for (int i = 0; i < fs_count; i++) {
        u8 temp_buffer[1536];
        memcpy(temp_buffer, &fs_cache[i], sizeof(FSNode));
        for (int j = 0; j < 3; j++) {
            memcpy(sector_buffer, temp_buffer + j * SECTOR_SIZE, SECTOR_SIZE);
            ata_write_sector(sector + j, sector_buffer);
        }
        sector = (i == fs_count - 1) ? 0 : FS_SECTOR_START + (i + 1) * 3;
        if (i < fs_count - 1) {
            fs_cache[i].next_sector = sector;
        } else {
            fs_cache[i].next_sector = 0;
        }
    }

    fs_dirty = 0;
}

void fs_mark_dirty() {
    fs_dirty = 1;
}

void fs_mkdir(const char* name) {
    if (fs_count >= MAX_FILES) {
        prints("Error: Maximum files reached\n");
        return;
    }

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, name) == 0) {
            prints("Error: Name already exists: ");
            prints(name);
            newline();
            return;
        }
    }

    strcpy(fs_cache[fs_count].name, name);
    fs_cache[fs_count].is_dir = 1;
    fs_cache[fs_count].content[0] = '\0';
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = 0;
    fs_count++;
    fs_mark_dirty();
}

void fs_touch(const char* name) {
    if (fs_count >= MAX_FILES) {
        prints("Error: Maximum files reached\n");
        return;
    }

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, name) == 0) {
            prints("Error: Name already exists: ");
            prints(name);
            newline();
            return;
        }
    }

    strcpy(fs_cache[fs_count].name, name);
    fs_cache[fs_count].is_dir = 0;
    fs_cache[fs_count].content[0] = '\0';
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = 0;
    fs_count++;
    fs_mark_dirty();
}

/* String functions */
void memcpy(void* dst, void* src, int len) {
    char* d = (char*)dst;
    char* s = (char*)src;
    while (len--) *d++ = *s++;
}

int strcmp(const char* a, const char* b) {
    while(*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strlen(const char* s) {
    const char* p = s;
    while(*p) p++;
    return p - s;
}

void strcpy(char* dst, const char* src) {
    while((*dst++ = *src++));
}

char* strcat(char* dest, const char* src) {
    char* ptr = dest;
    while(*ptr) ptr++;
    while((*ptr++ = *src++));
    return dest;
}

void itoa(int value, char* str, int base) {
    char* ptr = str, *ptr1 = str, tmp_char;
    int tmp_value;
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - value * base)];
    } while (value);
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while(ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

/* VGA output */
void clear_screen() {
    for(int r = 0; r < ROWS; r++)
        for(int c = 0; c < COLS; c++)
            VGA[r * COLS + c] = (unsigned short)(' ' | (text_color << 8));
    cursor_row = 0;
    cursor_col = 0;
}

void putchar(char ch) {
    if(ch == '\n') {
        cursor_col = 0;
        cursor_row++;
        if(cursor_row >= ROWS) {
            for(int r = 0; r < ROWS-1; r++) {
                for(int c = 0; c < COLS; c++) {
                    VGA[r * COLS + c] = VGA[(r+1) * COLS + c];
                }
            }
            for(int c = 0; c < COLS; c++) {
                VGA[(ROWS-1) * COLS + c] = (unsigned short)(' ' | (text_color << 8));
            }
            cursor_row = ROWS-1;
        }
        return;
    }
    VGA[cursor_row * COLS + cursor_col] = (unsigned short)(ch | (text_color << 8));
    cursor_col++;
    if(cursor_col >= COLS) {
        cursor_col = 0;
        cursor_row++;
        if(cursor_row >= ROWS) {
            for(int r = 0; r < ROWS-1; r++) {
                for(int c = 0; c < COLS; c++) {
                    VGA[r * COLS + c] = VGA[(r+1) * COLS + c];
                }
            }
            for(int c = 0; c < COLS; c++) {
                VGA[(ROWS-1) * COLS + c] = (unsigned short)(' ' | (text_color << 8));
            }
            cursor_row = ROWS-1;
        }
    }
}

void prints(const char* s) {
    for(const char* p = s; *p; p++) putchar(*p);
}

void newline() {
    putchar('\n');
}

/* Delay function */
void delay(int seconds) {
    for (volatile int i = 0; i < seconds * 10000000; i++);
}

/* Keyboard input */
static unsigned char shift_pressed = 0;

char keyboard_getchar() {
    while(1) {
        unsigned char st = inb(0x64);
        if(st & 1) {
            unsigned char sc = inb(0x60);
            if ((sc & 0x80) != 0) {
                sc &= 0x7F;
                if (sc == 0x2A || sc == 0x36) {
                    shift_pressed = 0;
                }
                continue;
            }
            if (sc == 0x2A || sc == 0x36) {
                shift_pressed = 1;
                continue;
            }
            static const char t[128] = {
                0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
                'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
                'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
                'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
            };
            static const char t_shift[128] = {
                0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
                'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
                'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
                'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
            };
            if(sc < 128 && t[sc]) {
                return shift_pressed ? t_shift[sc] : t[sc];
            }
        }
    }
}

/* Password input with masking */
void get_password(char* buffer, int max_len) {
    int idx = 0;
    buffer[0] = '\0';
    
    while(1) {
        char c = keyboard_getchar();
        if (c == '\n') {
            buffer[idx] = '\0';
            newline();
            return;
        } else if (c == '\b') {
            if (idx > 0) {
                idx--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 32 && c <= 126 && idx < max_len - 1) {
            buffer[idx] = c;
            idx++;
            buffer[idx] = '\0';
            putchar('*');
        }
    }
}

/* WexOS Installer */
void install_wexos() {
    text_color = 0x07;
    clear_screen();
    
    prints("===============================================\n");
    prints("          WexOS Installation Wizard\n");
    prints("===============================================\n\n");
    
    // Предупреждение о форматировании
    prints("WARNING: This will format ALL disks to WexFS filesystem!\n");
    prints("ALL existing data will be PERMANENTLY ERASED!\n\n");
    
    prints("Do you want to continue with installation? (y/N): ");
    
    char confirm = keyboard_getchar();
    putchar(confirm);
    newline();
    
    if (confirm != 'y' && confirm != 'Y') {
        prints("Installation cancelled.\n");
        return;
    }
    
    // Запрос пароля
    char password[64] = {0};
    char verify[64] = {0};
    
    prints("Do you want to set a password for your user? (y/N): ");
    char pass_confirm = keyboard_getchar();
    putchar(pass_confirm);
    newline();
    
    if (pass_confirm == 'y' || pass_confirm == 'Y') {
        while (1) {
            prints("Enter the password: ");
            get_password(password, sizeof(password));
            
            prints("Enter the password again: ");
            get_password(verify, sizeof(verify));
            
            if (strcmp(password, verify) == 0) {
                prints("Password set successfully!\n");
                break;
            } else {
                prints("Passwords do not match. Try again.\n");
            }
        }
    }
    
    // Начало установки
    prints("\nStarting installation...\n");
    prints("Formatting disks to WexFS...\n");
    
    // Инициализация файловой системы
    fs_count = 0;
    
    // Создание корневой директории
    strcpy(fs_cache[fs_count].name, "/");
    fs_cache[fs_count].is_dir = 1;
    fs_cache[fs_count].content[0] = '\0';
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = 0;
    fs_count++;
    
    // Создание системных директорий
    prints("Creating system directories...\n");
    fs_mkdir("home");
    fs_mkdir("home/user");
    fs_mkdir("home/user/desktop");
    fs_mkdir("home/user/desktop/RecycleBin");
    fs_mkdir("home/user/desktop/MyComputer");
    fs_mkdir("home/user/documents");
    fs_mkdir("home/user/downloads");
    fs_mkdir("boot");
    fs_mkdir("boot/Legacy");
    fs_mkdir("boot/UEFI");
    fs_mkdir("SystemRoot");
    fs_mkdir("SystemRoot/bin");
    fs_mkdir("SystemRoot/logs");
    fs_mkdir("SystemRoot/drivers");
    fs_mkdir("SystemRoot/kerneldrivers");
    fs_mkdir("SystemRoot/config");
    fs_mkdir("filesystem");
    fs_mkdir("filesystem/WexFs");
    
    // Создание системных файлов
    prints("Creating system files...\n");
    fs_touch("SystemRoot/bin/taskmgr.bin");
    fs_touch("SystemRoot/bin/kernel.bin");
    fs_touch("SystemRoot/bin/calc.bin");
    fs_touch("SystemRoot/logs/config.cfg");
    fs_touch("SystemRoot/drivers/keyboard.sys");
    fs_touch("SystemRoot/drivers/mouse.sys");
    fs_touch("SystemRoot/drivers/vga.sys");
    fs_touch("SystemRoot/kerneldrivers/kernel.sys");
    fs_touch("SystemRoot/kerneldrivers/ntrsys.sys");
    fs_touch("boot/uefi/grub.cfg");
    fs_touch("boot/Legacy/MBR.BIN");
    fs_touch("boot/Legacy/signature.cfg");
    fs_touch("filesystem/WexFs/touch.bin");
    fs_touch("filesystem/WexFs/mkdir.bin");
    fs_touch("filesystem/WexFs/size.bin");
    fs_touch("filesystem/WexFs/cd.bin");
    fs_touch("filesystem/WexFs/ls.bin");
    fs_touch("filesystem/WexFs/copy.bin");
    fs_touch("filesystem/WexFs/rm.bin");
    
    // Запись пароля если он задан
    if (password[0] != '\0') {
        fs_touch("SystemRoot/config/pass.cfg");
        // Находим файл пароля и записываем в него пароль
        for (int i = 0; i < fs_count; i++) {
            if (strcmp(fs_cache[i].name, "SystemRoot/config/pass.cfg") == 0 && !fs_cache[i].is_dir) {
                strcpy(fs_cache[i].content, password);
                fs_cache[i].size = strlen(password);
                break;
            }
        }
    }
    
    // Сохранение файловой системы на диск
    prints("Saving filesystem to disk...\n");
    fs_save_to_disk();
    
    prints("\n===============================================\n");
    prints("Installation completed successfully!\n");
    prints("WexOS has been installed on your system.\n");
    prints("Please reboot to start using WexOS.\n");
    prints("===============================================\n\n");
    
    prints("Press any key to reboot...");
    keyboard_getchar();
    
    // Перезагрузка системы
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}

/* Kernel main */
void _start() {
    text_color = 0x07;
    clear_screen();
    
    // Прямой запуск установщика
    install_wexos();
    
    // Если установка завершена или отменена, перезагружаемся
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}
