#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_FLAGS 0

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define NULL ((void*)0)
#define MAX_NAME 256
#define MAX_PATH 1024
#define SECTOR_SIZE 512
#define FS_SECTOR_START 1
#define MAX_FILES 64
#define MAX_HISTORY 10

typedef struct { 
    char name[MAX_PATH];
    int is_dir; 
    char content[256]; 
    u32 next_sector;
    u32 size;
} FSNode;

/* Function prototypes */
void itoa(int value, char* str, int base);
void coreview_command(void);
void putchar(char ch);
void memcpy(void* dst, void* src, int len);
int strcmp(const char* a, const char* b);
int strlen(const char* s);
void strcpy(char* dst, const char* src);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
char* strcat(char* dest, const char* src);
char* strrchr(const char* s, int c);
void prints(const char* s);
void newline();
void ata_read_sector(u32 lba, u8* buffer);
void ata_write_sector(u32 lba, u8* buffer);
void ata_wait_ready();
void ata_wait_drq();
void memory_command(void);
void clear_screen();
void fs_load_from_disk();
void fs_save_to_disk();
void fs_mark_dirty();
void fs_init();
void fs_ls();
void fs_mkdir(const char* name);
void fs_touch(const char* name);
void fs_rm(const char* name);
void fs_cd(const char* name);
FSNode* fs_find_file(const char* name);
void fs_copy(const char* src_name, const char* dest_name);
void fs_size(const char* name);

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
char current_dir[MAX_PATH] = "/";
int fs_dirty = 0;

/* Command history */
char command_history[MAX_HISTORY][128];
int history_count = 0;
int history_index = 0;

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
void fs_load_from_disk() {
    u8 sector_buffer[SECTOR_SIZE];
    u32 sector = FS_SECTOR_START;
    fs_count = 0;
    fs_dirty = 0;

    while (sector != 0 && fs_count < MAX_FILES) {
        u8 temp_buffer[1536];
        for (int i = 0; i < 3; i++) {
            ata_read_sector(sector + i, sector_buffer);
            memcpy(temp_buffer + i * SECTOR_SIZE, sector_buffer, SECTOR_SIZE);
        }
        memcpy(&fs_cache[fs_count], temp_buffer, sizeof(FSNode));
        sector = fs_cache[fs_count].next_sector;
        fs_count++;
    }

    if (fs_count == 0) {
        strcpy(fs_cache[0].name, "/");
        fs_cache[0].is_dir = 1;
        fs_cache[0].content[0] = '\0';
        fs_cache[0].next_sector = 0;
        fs_cache[0].size = 0;
        fs_count = 1;
        fs_dirty = 1;
        fs_save_to_disk();
    }
}

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

void fs_init() {
    fs_load_from_disk();
    strcpy(current_dir, "/");
}

void fs_ls() {
    prints("Contents of ");
    prints(current_dir);
    prints(":\n");

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(current_dir, "/") == 0) {
            char* slash = strchr(fs_cache[i].name, '/');
            if (slash == NULL || slash == fs_cache[i].name) {
                prints(fs_cache[i].name);
                if (fs_cache[i].is_dir) prints("/");
                newline();
            }
        } else {
            if (strstr(fs_cache[i].name, current_dir) == fs_cache[i].name) {
                char* name_part = fs_cache[i].name + strlen(current_dir);
                if (*name_part != '\0' && strchr(name_part, '/') == NULL) {
                    prints(name_part);
                    if (fs_cache[i].is_dir) prints("/");
                    newline();
                }
            }
        }
    }
}

void fs_mkdir(const char* name) {
    if (fs_count >= MAX_FILES) {
        prints("Error: Maximum files reached\n");
        return;
    }

    char full_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(name) >= MAX_NAME) {
            prints("Error: Name too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, name);
    } else {
        if (strlen(current_dir) + strlen(name) + 1 >= MAX_PATH) {
            prints("Error: Path too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, current_dir);
        strcat(full_path, name);
    }

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, full_path) == 0) {
            prints("Error: Name already exists: ");
            prints(name);
            newline();
            return;
        }
    }

    strcpy(fs_cache[fs_count].name, full_path);
    fs_cache[fs_count].is_dir = 1;
    fs_cache[fs_count].content[0] = '\0';
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = 0;
    fs_count++;
    fs_mark_dirty();
    fs_save_to_disk();
    prints("Directory '");
    prints(name);
    prints("' created\n");
}

void fs_touch(const char* name) {
    if (fs_count >= MAX_FILES) {
        prints("Error: Maximum files reached\n");
        return;
    }

    char full_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(name) >= MAX_NAME) {
            prints("Error: Name too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, name);
    } else {
        if (strlen(current_dir) + strlen(name) + 1 >= MAX_PATH) {
            prints("Error: Path too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, current_dir);
        strcat(full_path, name);
    }

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, full_path) == 0) {
            prints("Error: Name already exists: ");
            prints(name);
            newline();
            return;
        }
    }

    strcpy(fs_cache[fs_count].name, full_path);
    fs_cache[fs_count].is_dir = 0;
    fs_cache[fs_count].content[0] = '\0';
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = 0;
    fs_count++;
    fs_mark_dirty();
    fs_save_to_disk();
    prints("File '");
    prints(name);
    prints("' created\n");
}

void fs_rm(const char* name) {
    char full_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(name) >= MAX_NAME) {
            prints("Error: Name too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, name);
    } else {
        if (strlen(current_dir) + strlen(name) + 1 >= MAX_PATH) {
            prints("Error: Path too long: ");
            prints(name);
            newline();
            return;
        }
        strcpy(full_path, current_dir);
        strcat(full_path, name);
    }

    int found = -1;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, full_path) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1) {
        prints("Error: File or directory not found: ");
        prints(name);
        newline();
        return;
    }

    if (fs_cache[found].is_dir) {
        char dir_path[MAX_PATH];
        strcpy(dir_path, full_path);
        if (strcmp(full_path, "/") != 0) {
            if (strlen(dir_path) + 1 >= MAX_PATH) {
                prints("Error: Directory path too long\n");
                return;
            }
            strcat(dir_path, "/");
        }

        for (int i = fs_count - 1; i >= 0; i--) {
            if (i != found && strstr(fs_cache[i].name, dir_path) == fs_cache[i].name) {
                for (int j = i; j < fs_count - 1; j++) {
                    fs_cache[j] = fs_cache[j + 1];
                }
                fs_count--;
            }
        }
    }

    for (int i = found; i < fs_count - 1; i++) {
        fs_cache[i] = fs_cache[i + 1];
    }
    fs_count--;
    fs_mark_dirty();
    fs_save_to_disk();

    prints("'");
    prints(name);
    prints("' removed\n");
}

void fs_cd(const char* name) {
    if (strcmp(name, "..") == 0) {
        if (strcmp(current_dir, "/") != 0) {
            char* last_slash = strrchr(current_dir, '/');
            if (last_slash != NULL) {
                *last_slash = '\0';
                if (current_dir[0] == '\0') {
                    strcpy(current_dir, "/");
                }
            }
        }
    } else if (strcmp(name, "/") == 0) {
        strcpy(current_dir, "/");
    } else {
        char full_path[MAX_PATH];
        if (strcmp(current_dir, "/") == 0) {
            if (strlen(name) >= MAX_NAME) {
                prints("Error: Name too long: ");
                prints(name);
                newline();
                return;
            }
            strcpy(full_path, name);
        } else {
            if (strlen(current_dir) + strlen(name) + 1 >= MAX_PATH) {
                prints("Error: Path too long: ");
                prints(name);
                newline();
                return;
            }
            strcpy(full_path, current_dir);
            strcat(full_path, name);
        }

        int found = 0;
        for (int i = 0; i < fs_count; i++) {
            if (strcmp(fs_cache[i].name, full_path) == 0 && fs_cache[i].is_dir) {
                strcpy(current_dir, full_path);
                if (strcmp(full_path, "/") != 0) {
                    strcat(current_dir, "/");
                }
                found = 1;
                break;
            }
        }

        if (!found) {
            prints("Error: Directory not found: ");
            prints(name);
            newline();
        }
    }
}

FSNode* fs_find_file(const char* name) {
    char full_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(name) >= MAX_NAME) {
            prints("Error: Name too long: ");
            prints(name);
            newline();
            return NULL;
        }
        strcpy(full_path, name);
    } else {
        if (strlen(current_dir) + strlen(name) + 1 >= MAX_PATH) {
            prints("Error: Path too long: ");
            prints(name);
            newline();
            return NULL;
        }
        strcpy(full_path, current_dir);
        strcat(full_path, name);
    }

    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, full_path) == 0 && !fs_cache[i].is_dir) {
            return &fs_cache[i];
        }
    }
    return NULL;
}

void fs_copy(const char* src_name, const char* dest_name) {
    char src_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(src_name) >= MAX_NAME) {
            prints("Error: Source name too long: ");
            prints(src_name);
            newline();
            return;
        }
        strcpy(src_path, src_name);
    } else {
        if (strlen(current_dir) + strlen(src_name) + 1 >= MAX_PATH) {
            prints("Error: Source path too long: ");
            prints(src_name);
            newline();
            return;
        }
        strcpy(src_path, current_dir);
        strcat(src_path, src_name);
    }

    FSNode* src = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, src_path) == 0 && !fs_cache[i].is_dir) {
            src = &fs_cache[i];
            break;
        }
    }

    if (!src) {
        prints("Error: Source file not found: ");
        prints(src_name);
        newline();
        return;
    }

    if (fs_count >= MAX_FILES) {
        prints("Error: Maximum files reached\n");
        return;
    }

    char full_path[MAX_PATH];
    if (strcmp(current_dir, "/") == 0) {
        if (strlen(dest_name) >= MAX_NAME) {
            prints("Error: Destination name too long: ");
            prints(dest_name);
            newline();
            return;
        }
        strcpy(full_path, dest_name);
    } else {
        if (strlen(current_dir) + strlen(dest_name) + 1 >= MAX_PATH) {
            prints("Error: Destination path too long: ");
            prints(dest_name);
            newline();
            return;
        }
        strcpy(full_path, current_dir);
        strcat(full_path, dest_name);
    }

    strcpy(fs_cache[fs_count].name, full_path);
    fs_cache[fs_count].is_dir = 0;
    strcpy(fs_cache[fs_count].content, src->content);
    fs_cache[fs_count].next_sector = 0;
    fs_cache[fs_count].size = src->size;
    fs_count++;
    fs_mark_dirty();
    fs_save_to_disk();
    prints("File copied to '");
    prints(dest_name);
    prints("'\n");
}

void fs_size(const char* name) {
    FSNode* file = fs_find_file(name);
    if (!file) {
        prints("Error: File not found: ");
        prints(name);
        newline();
        return;
    }
    char size_str[16];
    itoa(file->size / 1024, size_str, 10);
    prints("File size: ");
    prints(size_str);
    prints(" KB\n");
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

char* strchr(const char* s, int c) {
    while(*s) {
        if(*s == c) return (char*)s;
        s++;
    }
    return NULL;
}

char* strstr(const char* haystack, const char* needle) {
    if(!*needle) return (char*)haystack;
    for(const char* p = haystack; *p; p++) {
        const char* h = p, *n = needle;
        while(*h && *n && *h == *n) { h++; n++; }
        if(!*n) return (char*)p;
    }
    return NULL;
}

char* strcat(char* dest, const char* src) {
    char* ptr = dest;
    while(*ptr) ptr++;
    while((*ptr++ = *src++));
    return dest;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while(*s) {
        if(*s == c) last = s;
        s++;
    }
    return (char*)last;
}

int atoi(const char* s) {
    int r = 0;
    while(*s >= '0' && *s <= '9') {
        r = r * 10 + (*s - '0');
        s++;
    }
    return r;
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

/* RTC functions */
unsigned char rtc_read(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

unsigned char bcd_to_bin(unsigned char val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void get_rtc_time() {
    while (1) {
        outb(0x70, 0x0A);
        if (!(inb(0x71) & 0x80)) break;
    }

    unsigned char second = rtc_read(0x00);
    unsigned char minute = rtc_read(0x02);
    unsigned char hour = rtc_read(0x04);
    unsigned char day = rtc_read(0x07);
    unsigned char month = rtc_read(0x08);
    unsigned char year = rtc_read(0x09);
    unsigned char century = rtc_read(0x32);

    second = bcd_to_bin(second);
    minute = bcd_to_bin(minute);
    hour = bcd_to_bin(hour);
    day = bcd_to_bin(day);
    month = bcd_to_bin(month);
    year = bcd_to_bin(year);
    century = bcd_to_bin(century);

    int full_year = century * 100 + year;

    char buf[50];
    itoa(full_year, buf, 10);
    prints(buf);
    prints("-");
    itoa(month, buf, 10);
    if (month < 10) prints("0");
    prints(buf);
    prints("-");
    itoa(day, buf, 10);
    if (day < 10) prints("0");
    prints(buf);
    prints(" ");
    itoa(hour, buf, 10);
    if (hour < 10) prints("0");
    prints(buf);
    prints(":");
    itoa(minute, buf, 10);
    if (minute < 10) prints("0");
    prints(buf);
    prints(":");
    itoa(second, buf, 10);
    if (second < 10) prints("0");
    prints(buf);
    newline();
}

void time_command() {
    while (1) {
        outb(0x70, 0x0A);
        if (!(inb(0x71) & 0x80)) break;
    }

    unsigned char second = rtc_read(0x00);
    unsigned char minute = rtc_read(0x02);
    unsigned char hour = rtc_read(0x04);

    second = bcd_to_bin(second);
    minute = bcd_to_bin(minute);
    hour = bcd_to_bin(hour);

    char buf[20];
    itoa(hour, buf, 10);
    if (hour < 10) prints("0");
    prints(buf);
    prints(":");
    itoa(minute, buf, 10);
    if (minute < 10) prints("0");
    prints(buf);
    prints(":");
    itoa(second, buf, 10);
    if (second < 10) prints("0");
    prints(buf);
    newline();
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
            if (sc == 0x43) return '\xF9';
        }
    }
}

char getch_with_arrows() {
    static unsigned char extended = 0;
    while(1) {
        unsigned char st = inb(0x64);
        if (st & 1) {
            unsigned char sc = inb(0x60);
            if ((sc & 0x80) != 0) {
                sc &= 0x7F;
                if (sc == 0x2A || sc == 0x36) shift_pressed = 0;
                extended = 0;
                continue;
            }
            if (sc == 0x2A || sc == 0x36) {
                shift_pressed = 1;
                continue;
            }
            if (sc == 0xE0) {
                extended = 1;
                continue;
            }
            if (extended) {
                extended = 0;
                if (sc == 0x48) return 'U';
                if (sc == 0x50) return 'D';
                if (sc == 0x4B) return 'L';
                if (sc == 0x4D) return 'R';
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
            if (sc < 128 && t[sc]) {
                return shift_pressed ? t_shift[sc] : t[sc];
            }
            if (sc == 0x43) return '\xF9';
        }
    }
}

/* System commands */
void reboot_system() {
    prints("Rebooting...\n");
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}

void shutdown_system() {
    prints("Shutdown...\n");
    outb(0x604, 0x2000 & 0xFF);
    outb(0x604, (0x2000 >> 8) & 0xFF);
    outb(0x400, 0x00);
    while(1) { __asm__ volatile("hlt"); }
}

/* Processes simulation */
typedef struct {
    char name[32];
    int pid;
} Process;

Process processes[10];
int process_count = 0;

void init_processes() {
    strcpy(processes[0].name, "kernel.bin");
    processes[0].pid = 1;
    strcpy(processes[1].name, "shell");
    processes[1].pid = 2;
    process_count = 2;
}

void ps_command() {
    prints("PID\tName\n");
    for (int i = 0; i < process_count; i++) {
        char pid_str[10];
        itoa(processes[i].pid, pid_str, 10);
        prints(pid_str);
        putchar('\t');
        prints(processes[i].name);
        newline();
    }
}

void kill_command(const char* arg) {
    if (strcmp(arg, "kernel.bin") == 0) {
        text_color = 0x4F;
        clear_screen();
        prints("0x000DIED\n");
        prints("Critical Error: CRITICAL_PROCESS_DIED\n");
        prints("Details: Attempted to kill critical kernel process.\n");
        prints("System will reboot in 10 seconds.\n");
        delay(999999); 
        reboot_system();
    } else if (strcmp(arg, "shell") == 0) {
        text_color = 0x4F;
        clear_screen();
        prints("0xER00DI\n");
        prints("Error: SYSTEM_PROCESS_DIED\n");
        prints("Details: You have completed an important system process and the system cannot continue working.\n");
        prints("The shell creation process is completed, which caused the error.\n");
        prints("System will reboot in 10 seconds.\n");
        delay(999999); 
        reboot_system();
    } else {
        int found = 0;
        for (int i = 0; i < process_count; i++) {
            if (strcmp(processes[i].name, arg) == 0 || atoi(arg) == processes[i].pid) {
                for (int j = i; j < process_count - 1; j++) {
                    processes[j] = processes[j + 1];
                }
                process_count--;
                found = 1;
                prints("Process killed: ");
                prints(arg);
                newline();
                break;
            }
        }
        if (!found) {
            prints("Process not found: ");
            prints(arg);
            newline();
        }
    }
}

/* CPU information command */
void cpu_command() {
    char vendor[13];
    u32 eax, ebx, ecx, edx;

    __asm__ volatile("mov $0, %%eax; cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    vendor[0] = (ebx >> 0) & 0xFF;
    vendor[1] = (ebx >> 8) & 0xFF;
    vendor[2] = (ebx >> 16) & 0xFF;
    vendor[3] = (ebx >> 24) & 0xFF;
    vendor[4] = (edx >> 0) & 0xFF;
    vendor[5] = (edx >> 8) & 0xFF;
    vendor[6] = (edx >> 16) & 0xFF;
    vendor[7] = (edx >> 24) & 0xFF;
    vendor[8] = (ecx >> 0) & 0xFF;
    vendor[9] = (ecx >> 8) & 0xFF;
    vendor[10] = (ecx >> 16) & 0xFF;
    vendor[11] = (ecx >> 24) & 0xFF;
    vendor[12] = '\0';

    prints("CPU Information:\n");
    prints("Vendor: ");
    prints(vendor);
    newline();
    prints("Architecture: x86\n");
    prints("Cores: 1\n");
    prints("Frequency: 1.0 GHz\n");
    prints("Features: MMU, FPU\n");
}

/* Coreview command */
void coreview_command(void) {
    u32 eax, ebx, ecx, edx, eip, esp, ebp, eflags;
    u16 cs, ds, es;

    __asm__ volatile("mov %%eax, %0" : "=r"(eax));
    __asm__ volatile("mov %%ebx, %0" : "=r"(ebx));
    __asm__ volatile("mov %%ecx, %0" : "=r"(ecx));
    __asm__ volatile("mov %%edx, %0" : "=r"(edx));
    __asm__ volatile("call 1f; 1: pop %0" : "=r"(eip));
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile("mov %%es, %0" : "=r"(es));
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));

    prints("Core Information:\n");
    prints("General-Purpose Registers:\n");
    char buf[16];
    prints("  EAX: 0x"); itoa(eax, buf, 16); prints(buf); newline();
    prints("  EBX: 0x"); itoa(ebx, buf, 16); prints(buf); newline();
    prints("  ECX: 0x"); itoa(ecx, buf, 16); prints(buf); newline();
    prints("  EDX: 0x"); itoa(edx, buf, 16); prints(buf); newline();
    prints("Pointer Registers:\n");
    prints("  EIP: 0x"); itoa(eip, buf, 16); prints(buf); newline();
    prints("  ESP: 0x"); itoa(esp, buf, 16); prints(buf); newline();
    prints("  EBP: 0x"); itoa(ebp, buf, 16); prints(buf); newline();
    prints("Segment Registers:\n");
    prints("  CS: 0x"); itoa(cs, buf, 16); prints(buf); newline();
    prints("  DS: 0x"); itoa(ds, buf, 16); prints(buf); newline();
    prints("  ES: 0x"); itoa(es, buf, 16); prints(buf); newline();
    prints("Flags:\n");
    prints("  EFLAGS: 0x"); itoa(eflags, buf, 16); prints(buf); newline();
}

/* OS version command */
void osver_command() {
    prints("OS Version: WexOS TinyShell v0.6\n");
    prints("Build Date: 2025-09-16\n");
}

/* Date command without network */
void date_command() {
    prints("Current date (local): ");
    
    while (1) {
        outb(0x70, 0x0A);
        if (!(inb(0x71) & 0x80)) break;
    }

    unsigned char day = rtc_read(0x07);
    unsigned char month = rtc_read(0x08);
    unsigned char year = rtc_read(0x09);
    unsigned char century = rtc_read(0x32);

    day = bcd_to_bin(day);
    month = bcd_to_bin(month);
    year = bcd_to_bin(year);
    century = bcd_to_bin(century);

    int full_year = century * 100 + year;

    char buf[20];
    itoa(full_year, buf, 10);
    prints(buf);
    prints("-");
    itoa(month, buf, 10);
    if (month < 10) prints("0");
    prints(buf);
    prints("-");
    itoa(day, buf, 10);
    if (day < 10) prints("0");
    prints(buf);
    newline();
}

/* BIOS version command */
void biosver_command() {
    prints("BIOS Version: WexOS Virtual BIOS v1.0\n");
    prints("Build Date: 2023-12-01\n");
    prints("SMBIOS: 2.8\n");
}

/* Calculator command */
void calc_command(const char* expression) {
    if (!expression || !*expression) {
        prints("Usage: calc <expression>\n");
        prints("Example: calc 2*2+3/1\n");
        return;
    }
    
    char expr[128];
    strcpy(expr, expression);
    
    char* dst = expr;
    char* src = expr;
    while (*src) {
        if (*src != ' ') {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
    
    for (char* p = expr; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 'a' + 'A';
        }
    }
    
    int result = 0;
    int current = 0;
    char op = '+';
    char* p = expr;
    
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            current = current * 10 + (*p - '0');
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            if (op == '+') result += current;
            else if (op == '-') result -= current;
            else if (op == '*') result *= current;
            else if (op == '/') {
                if (current == 0) {
                    prints("Error: Division by zero\n");
                    return;
                }
                result /= current;
            }
            op = *p;
            current = 0;
        } else {
            prints("Error: Invalid character '");
            putchar(*p);
            prints("' in expression\n");
            return;
        }
        p++;
    }
    
    if (op == '+') result += current;
    else if (op == '-') result -= current;
    else if (op == '*') result *= current;
    else if (op == '/') {
        if (current == 0) {
            prints("Error: Division by zero\n");
            return;
        }
        result /= current;
    }
    
    prints(expression);
    prints(" = ");
    char result_str[20];
    itoa(result, result_str, 10);
    prints(result_str);
    newline();
}

/* System Configuration */
typedef struct {
    char user_name[32];
    char user_pass[32];
    char admin_pass[32];
    char root_pass[32];
    int screen_timeout;
    int max_users;
    int auto_login;
    int debug_mode;
    int log_level;
} SystemConfig;

SystemConfig sys_config = {
    "User", "pass", "admin123", "root123", 300, 10, 0, 0, 1
};

SystemConfig temp_config;

/* Install functions */
void install_virtual() {
    fs_init();
    fs_mkdir("SystemRoot");
    fs_mkdir("SystemRoot/bin");
    fs_touch("SystemRoot/bin/taskmgr");
    fs_touch("SystemRoot/bin/config");
    fs_touch("SystemRoot/kernel.sys");
    
    fs_touch("help.txt");
    FSNode* help_file = fs_find_file("help.txt");
    if (help_file) {
        strcpy(help_file->content, 
            "WexOS TinyShell Commands:\n"
            "help - Show this help message\n"
            "echo [text] - Echo text to screen\n"
            "reboot - Reboot the system\n"
            "shutdown - Shutdown the system\n"
            "clear - Clear the screen\n"
            "ls - List files\n"
            "cd [dir] - Change directory\n"
            "mkdir [name] - Create directory\n"
            "rm [name] - Remove file/directory\n"
            "touch [name] - Create empty file\n"
            "copy [src] [dest] - Copy file\n"
            "writer [file] - Edit file\n"
            "ps - List processes\n"
            "kill [pid/name] - Kill process\n"
            "coreview - Show CPU core registers\n"
            "color [0-7] - Set text color\n"
            "colorf [0-7] - Set foreground color\n"
            "install [0/1] - Install system\n"
            "config - System configuration\n"
            "cpu - Show CPU information\n"
            "date - Show date without network\n"
            "biosver - Show BIOS version\n"
            "calc [expression] - Calculator\n"
            "time - Show time without network\n"
            "size [file] - Show file size in KB\n"
            "osver - Show OS version\n"
            "history - Show command history\n"
        );
        help_file->size = strlen(help_file->content);
        fs_mark_dirty();
    }
    
    prints("System installed to virtual FS\n");
    prints("Help file 'help.txt' created\n");
    fs_save_to_disk();
}

void install_disk() {
    prints("\nWARNING: ALL DISKS INCLUDING BOOT DISKS WILL BE FORMATTED TO FAT32 FOR OS INSTALLATION.\n");
    prints("CONTINUE? Y/N: ");
    
    char confirm = keyboard_getchar();
    putchar(confirm);
    newline();
    
    if (confirm == 'Y' || confirm == 'y') {
        prints("Formatting disks to FAT32...\n");
        prints("Copying system files...\n");
        prints("Installing kernel...\n");
        prints("Installation completed successfully!\n");
        prints("Please reboot to start the installed system.\n");
        fs_save_to_disk();
    } else {
        prints("Installation cancelled.\n");
    }
}

/* String input function */
void get_input(char* buffer, int max_len, int row, int col, int field_len) {
    int idx = strlen(buffer);
    
    while(1) {
        cursor_row = row;
        cursor_col = col;
        for(int i = 0; i < field_len; i++) {
            putchar(' ');
        }
        
        cursor_row = row;
        cursor_col = col;
        prints(buffer);
        
        cursor_row = row;
        cursor_col = col + idx;
        
        char c = keyboard_getchar();
        if (c == '\n') {
            break;
        } else if (c == '\b') {
            if (idx > 0) {
                idx--;
                buffer[idx] = '\0';
            }
        } else if (c >= 32 && c <= 126 && idx < max_len - 1) {
            buffer[idx] = c;
            idx++;
            buffer[idx] = '\0';
        }
    }
}

/* Number input function */
void get_number_input(int* value, int min, int max, int row, int col, int field_len) {
    char buffer[16];
    itoa(*value, buffer, 10);
    int idx = strlen(buffer);
    
    while(1) {
        cursor_row = row;
        cursor_col = col;
        for(int i = 0; i < field_len; i++) {
            putchar(' ');
        }
        
        cursor_row = row;
        cursor_col = col;
        prints(buffer);
        
        cursor_row = row;
        cursor_col = col + idx;
        
        char c = keyboard_getchar();
        if (c == '\n') {
            int new_val = atoi(buffer);
            if (new_val >= min && new_val <= max) {
                *value = new_val;
                break;
            }
        } else if (c == '\b') {
            if (idx > 0) {
                idx--;
                buffer[idx] = '\0';
            }
        } else if (c >= '0' && c <= '9') {
            if (idx < 15) {
                buffer[idx] = c;
                idx++;
                buffer[idx] = '\0';
                int new_val = atoi(buffer);
                if (new_val > max) {
                    idx--;
                    buffer[idx] = '\0';
                }
            }
        }
    }
}

/* Writer text editor */
void writer_command(const char* filename) {
    FSNode* file = fs_find_file(filename);
    if (!file) {
        prints("Error: File not found: ");
        prints(filename);
        newline();
        return;
    }
    
    char content[256];
    strcpy(content, file->content);
    int content_len = strlen(content);
    int cursor_pos = content_len;
    
    unsigned char old_color = text_color;
    int exit_editor = 0;
    int save_file = 0;
    
    text_color = 0x70;
    clear_screen();
    
    // Верхняя строка с названием файла
    for (int c = 0; c < COLS; c++) {
        VGA[0 * COLS + c] = (unsigned short)(' ' | (text_color << 8));
    }
    cursor_row = 0;
    cursor_col = (COLS - strlen(filename) - 12) / 2;
    prints("File: ");
    prints(filename);
    prints(" - Writer");
    
    // Нижняя строка с подсказками
    text_color = 0x70;
    cursor_row = ROWS - 1;
    cursor_col = 5;
    prints("F9: Save  ESC: Exit");
    
    // Основная область редактирования
    text_color = 0x1F;
    for (int r = 1; r < ROWS - 1; r++) {
        for (int c = 0; c < COLS; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (text_color << 8));
        }
    }
    
    // Отображаем начальное содержимое
    cursor_row = 2;
    cursor_col = 2;
    for (int i = 0; i < content_len; i++) {
        if (content[i] == '\n') {
            cursor_row++;
            cursor_col = 2;
        } else {
            putchar(content[i]);
        }
    }
    
    // Основной цикл редактора
    while (!exit_editor) {
        cursor_row = 2 + (cursor_pos / (COLS - 4));
        cursor_col = 2 + (cursor_pos % (COLS - 4));
        
        char c = keyboard_getchar();
        
        switch (c) {
            case 27: // ESC
                exit_editor = 1;
                break;
                
            case '\n': // Enter
                if (content_len < 255) {
                    for (int i = content_len; i > cursor_pos; i--) {
                        content[i] = content[i-1];
                    }
                    content[cursor_pos] = '\n';
                    content_len++;
                    cursor_pos++;
                    content[content_len] = '\0';
                    
                    // Перерисовываем содержимое
                    text_color = 0x1F;
                    for (int r = 1; r < ROWS - 1; r++) {
                        for (int c = 0; c < COLS; c++) {
                            VGA[r * COLS + c] = (unsigned short)(' ' | (text_color << 8));
                        }
                    }
                    cursor_row = 2;
                    cursor_col = 2;
                    for (int i = 0; i < content_len; i++) {
                        if (content[i] == '\n') {
                            cursor_row++;
                            cursor_col = 2;
                        } else {
                            putchar(content[i]);
                        }
                    }
                }
                break;
                
            case '\b': // Backspace
                if (cursor_pos > 0 && content_len > 0) {
                    for (int i = cursor_pos - 1; i < content_len; i++) {
                        content[i] = content[i+1];
                    }
                    content_len--;
                    cursor_pos--;
                    content[content_len] = '\0';
                    
                    // Перерисовываем содержимое
                    text_color = 0x1F;
                    for (int r = 1; r < ROWS - 1; r++) {
                        for (int c = 0; c < COLS; c++) {
                            VGA[r * COLS + c] = (unsigned short)(' ' | (text_color << 8));
                        }
                    }
                    cursor_row = 2;
                    cursor_col = 2;
                    for (int i = 0; i < content_len; i++) {
                        if (content[i] == '\n') {
                            cursor_row++;
                            cursor_col = 2;
                        } else {
                            putchar(content[i]);
                        }
                    }
                }
                break;
                
            case '\xF9': // F9 - Save
                save_file = 1;
                exit_editor = 1;
                break;
                
            default: // Обычные символы
                if (c >= 32 && c <= 126 && content_len < 255) {
                    for (int i = content_len; i > cursor_pos; i--) {
                        content[i] = content[i-1];
                    }
                    content[cursor_pos] = c;
                    content_len++;
                    cursor_pos++;
                    content[content_len] = '\0';
                    
                    // Перерисовываем содержимое
                    text_color = 0x1F;
                    for (int r = 1; r < ROWS - 1; r++) {
                        for (int c = 0; c < COLS; c++) {
                            VGA[r * COLS + c] = (unsigned short)(' ' | (text_color << 8));
                        }
                    }
                    cursor_row = 2;
                    cursor_col = 2;
                    for (int i = 0; i < content_len; i++) {
                        if (content[i] == '\n') {
                            cursor_row++;
                            cursor_col = 2;
                        } else {
                            putchar(content[i]);
                        }
                    }
                }
                break;
        }
    }
    
    // Сохранение файла
    if (save_file) {
        if (strlen(content) < sizeof(file->content)) {
            strcpy(file->content, content);
            file->size = strlen(content);
            fs_mark_dirty();
            fs_save_to_disk();
            prints("\nFile saved: ");
            prints(filename);
            newline();
        } else {
            prints("\nError: File content too large\n");
        }
    }
    
    text_color = old_color;
    clear_screen();
}

/* Configuration screen */
void draw_config_screen(int current_field) {
    text_color = 0x1F;
    clear_screen();
    
    cursor_row = 0;
    cursor_col = (COLS - 18) / 2;
    prints("{SYSTEM CONFIGURATIONS}");
    
    cursor_row = 2; cursor_col = 5;
    prints("User-Name: "); prints(temp_config.user_name);
    cursor_row = 3; cursor_col = 5;
    prints("User-Pass: "); prints("********");
    cursor_row = 4; cursor_col = 5;
    prints("Administration-Pass: "); prints("********");
    cursor_row = 5; cursor_col = 5;
    prints("Root-Pass: "); prints("********");
    cursor_row = 6; cursor_col = 5;
    prints("Screen-Timeout: "); 
    char timeout_str[10];
    itoa(temp_config.screen_timeout, timeout_str, 10);
    prints(timeout_str); prints(" sec");
    cursor_row = 7; cursor_col = 5;
    prints("Max-Users: "); 
    char users_str[10];
    itoa(temp_config.max_users, users_str, 10);
    prints(users_str);
    cursor_row = 8; cursor_col = 5;
    prints("Auto-Login: "); prints(temp_config.auto_login ? "Enabled" : "Disabled");
    cursor_row = 9; cursor_col = 5;
    prints("Debug-Mode: "); prints(temp_config.debug_mode ? "Enabled" : "Disabled");
    cursor_row = 10; cursor_col = 5;
    prints("Log-Level: "); 
    char level_str[2] = { '0' + temp_config.log_level, 0 };
    prints(level_str);
    
    cursor_row = current_field + 2;
    cursor_col = 5;
    text_color = 0x4F;
    switch(current_field) {
        case 0: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("User-Name: ");
            break;
        case 1: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("User-Pass: ");
            break;
        case 2: 
            for(int i=0;i<30;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Administration-Pass: ");
            break;
        case 3: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Root-Pass: ");
            break;
        case 4: 
            for(int i=0;i<25;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Screen-Timeout: ");
            break;
        case 5: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Max-Users: ");
            break;
        case 6: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Auto-Login: ");
            break;
        case 7: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Debug-Mode: ");
            break;
        case 8: 
            for(int i=0;i<20;i++) putchar(' ');
            cursor_row = current_field + 2;
            cursor_col = 5;
            prints("Log-Level: ");
            break;
    }
    text_color = 0x1F;
    
    cursor_row = 20; cursor_col = 5;
    prints("UP/DOWN: Select  ENTER: Edit  F9: Save  ESC: Exit");
}

void edit_config_field(int field_num) {
    cursor_row = field_num + 2;
    cursor_col = 25;
    text_color = 0x4F;
    
    switch(field_num) {
        case 0: 
            get_input(temp_config.user_name, 31, field_num + 2, 25, 30);
            break;
        case 1: 
            get_input(temp_config.user_pass, 31, field_num + 2, 25, 30);
            break;
        case 2: 
            get_input(temp_config.admin_pass, 31, field_num + 2, 25, 30);
            break;
        case 3: 
            get_input(temp_config.root_pass, 31, field_num + 2, 25, 30);
            break;
        case 4: 
            get_number_input(&temp_config.screen_timeout, 60, 3600, field_num + 2, 25, 6);
            break;
        case 5: 
            get_number_input(&temp_config.max_users, 1, 100, field_num + 2, 25, 3);
            break;
        case 6: 
            temp_config.auto_login = !temp_config.auto_login;
            break;
        case 7: 
            temp_config.debug_mode = !temp_config.debug_mode;
            break;
        case 8: 
            get_number_input(&temp_config.log_level, 0, 3, field_num + 2, 25, 1);
            break;
    }
    
    text_color = 0x1F;
}

void config_command() {
    temp_config = sys_config;
    
    int current_field = 0;
    int exit_config = 0;
    int save_config = 0;
    
    while(!exit_config) {
        draw_config_screen(current_field);
        
        char key = getch_with_arrows();
        
        switch(key) {
            case 'U': 
                current_field = (current_field == 0) ? 8 : current_field - 1;
                break;
            case 'D': 
                current_field = (current_field == 8) ? 0 : current_field + 1;
                break;
            case '\n': 
                edit_config_field(current_field);
                break;
            case '\xF9': 
                save_config = 1;
                exit_config = 1;
                break;
            case 27: 
                exit_config = 1;
                break;
        }
    }
    
    if (save_config) {
        sys_config = temp_config;
        cursor_row = 17; cursor_col = 5;
        prints("Configuration saved!");
    } else {
        cursor_row = 17; cursor_col = 5;
        prints("Changes discarded!");
    }
    
    cursor_row = 19; cursor_col = 5;
    prints("Press any key to continue...");
    keyboard_getchar();
    
    text_color = 0x07;
    clear_screen();
}

void watch_command() {
    unsigned char old_color = text_color;
    text_color = 0x0A; // Зеленый цвет

    // Начальная отрисовка статичного ASCII-арта "WexOS"
    clear_screen();
    cursor_row = 1; cursor_col = 20;
    prints(" __      __ _______  ____  ");
    cursor_row++; cursor_col = 20;
    prints("|  |    |  |  ___  ||  _ \\ ");
    cursor_row++; cursor_col = 20;
    prints("|  |    |  | |___| || | | |");
    cursor_row++; cursor_col = 20;
    prints("|  |___ |  | |____ || | | |");
    cursor_row++; cursor_col = 20;
    prints("|______||__|_______||_| |_|");

    while (1) {
        // Проверка на ESC
        unsigned char st = inb(0x64);
        if (st & 1) {
            unsigned char sc = inb(0x60);
            if (sc == 0x01) { // ESC код
                text_color = old_color;
                clear_screen();
                prints("> ");
                return;
            }
        }

        // Ожидание обновления RTC
        outb(0x70, 0x0A);
        while (inb(0x71) & 0x80);

        // Чтение времени и даты из RTC
        unsigned char second = bcd_to_bin(rtc_read(0x00));
        unsigned char minute = bcd_to_bin(rtc_read(0x02));
        unsigned char hour = bcd_to_bin(rtc_read(0x04));
        unsigned char day = bcd_to_bin(rtc_read(0x07));
        unsigned char month = bcd_to_bin(rtc_read(0x08));
        unsigned char year = bcd_to_bin(rtc_read(0x09));
        unsigned char century = bcd_to_bin(rtc_read(0x32));

        int full_year = century * 100 + year;

        // Очистка только области времени
        cursor_row = 8; cursor_col = 30;
        for (int i = 0; i < 20; i++) putchar(' ');

        // Отображение времени
        char time_buf[50];
        char tmp[10];
        strcpy(time_buf, "Time: ");
        itoa(hour, tmp, 10);
        if (hour < 10) strcat(time_buf, "0");
        strcat(time_buf, tmp);
        strcat(time_buf, ":");
        itoa(minute, tmp, 10);
        if (minute < 10) strcat(time_buf, "0");
        strcat(time_buf, tmp);
        strcat(time_buf, ":");
        itoa(second, tmp, 10);
        if (second < 10) strcat(time_buf, "0");
        strcat(time_buf, tmp);

        cursor_row = 8; cursor_col = 30;
        prints(time_buf);

        // Очистка только области даты
        cursor_row = 10; cursor_col = 30;
        for (int i = 0; i < 20; i++) putchar(' ');

        // Отображение даты
        char date_buf[50];
        strcpy(date_buf, "Date: ");
        itoa(full_year, tmp, 10);
        strcat(date_buf, tmp);
        strcat(date_buf, "-");
        itoa(month, tmp, 10);
        if (month < 10) strcat(date_buf, "0");
        strcat(date_buf, tmp);
        strcat(date_buf, "-");
        itoa(day, tmp, 10);
        if (day < 10) strcat(date_buf, "0");
        strcat(date_buf, tmp);

        cursor_row = 10; cursor_col = 30;
        prints(date_buf);

        // Инструкция выхода
        cursor_row = 12; cursor_col = 30;
        prints("Press ESC to exit");

        delay(1); // Обновление каждую секунду
    }
}

/* Command history function */
void history_command() {
    prints("Command History:\n");
    for (int i = 0; i < history_count; i++) {
        char index_str[10];
        itoa(i + 1, index_str, 10);
        prints(index_str);
        prints(": ");
        prints(command_history[i]);
        newline();
    }
}

/* Case-insensitive string comparison */
int strcasecmp(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = ca - 'a' + 'A';
        if (cb >= 'a' && cb <= 'z') cb = cb - 'a' + 'A';
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Command help */
void show_help() {
    unsigned char old_color = text_color;
    text_color = 0x0A;
    
    const char* help_lines[] = {
        "Available commands:",
        "  help     - Show this help message",
        "  echo     - Echo text to screen",
        "  reboot   - Reboot the system",
        "  shutdown - Shutdown the system",
        "  clear    - Clear the screen",
        "  ls       - List files in current directory",
        "  cd       - Change directory",
        "  mkdir    - Create directory",
        "  rm       - Remove file/directory",
        "  touch    - Create empty file",
        "  copy     - Copy file (copy src dest)",
        "  writer   - Edit file content",
        "  ps       - List processes",
        "  kill     - Kill process",
        "  coreview - Show CPU core registers",
        "  color    - Set text color (0-7)",
        "  colorf   - Set foreground color (0-7)",
        "  install  - Install system (0=virtual, 1=disk)",
        "  config   - System configuration menu",
		"  memory   - Memory dump",
        "  cpu      - Show CPU information",
        "  date     - Show date without network",
		"  watch    - A watch with an easy interface",
        "  biosver  - Show BIOS version",
        "  calc     - Calculator (calc expression)",
        "  time     - Show time without network",
        "  size     - Show file size in KB",
        "  osver    - Show OS version",
        "  history  - Show command history",
        NULL
    };
    
    for (int i = 0; help_lines[i] != NULL; i++) {
        prints(help_lines[i]);
        newline();
        for (volatile int d = 0; d < 1000000; d++);
    }
    
    text_color = old_color;
}

/* Trim whitespace */
void trim_whitespace(char* str) {
    char* end;
    while(*str == ' ') str++;
    end = str + strlen(str) - 1;
    while(end > str && *end == ' ') end--;
    *(end + 1) = '\0';
}

/* Split arguments */
void split_args(char* args, char** arg1, char** arg2) {
    *arg1 = args;
    while (*args && *args != ' ') args++;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
        *arg2 = args;
    } else {
        *arg2 = NULL;
    }
}

/* Command parser */
void run_command(char* line) {
    trim_whitespace(line);
    if(*line == 0) return;
    
    /* Save command to history */
    if (history_count < MAX_HISTORY) {
        strcpy(command_history[history_count], line);
        history_count++;
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(command_history[i], command_history[i + 1]);
        }
        strcpy(command_history[MAX_HISTORY - 1], line);
    }
    
    char* p = line;
    while(*p && *p != ' ') p++;
    char saved = 0;
    if(*p) { saved = *p; *p = 0; p++; }
    
    char command_upper[128];
    strcpy(command_upper, line);
    for(char* c = command_upper; *c; c++) {
        if(*c >= 'a' && *c <= 'z') *c = *c - 'a' + 'A';
    }
    
    if(strcasecmp(line, "help") == 0) show_help();
    else if(strcasecmp(line, "echo") == 0) { while(*p == ' ') p++; prints(p); newline(); }
    else if(strcasecmp(line, "reboot") == 0) reboot_system();
    else if(strcasecmp(line, "shutdown") == 0) shutdown_system();
	else if(strcasecmp(line, "watch") == 0) watch_command();
    else if(strcasecmp(line, "clear") == 0) clear_screen();
	else if(strcasecmp(line, "memory") == 0) memory_command();
    else if(strcasecmp(line, "ls") == 0) fs_ls();
    else if(strcasecmp(line, "cd") == 0) { while(*p == ' ') p++; if(*p) fs_cd(p); else prints("Usage: cd <directory>\n"); }
    else if(strcasecmp(line, "mkdir") == 0) { while(*p == ' ') p++; if(*p) fs_mkdir(p); else prints("Usage: mkdir <name>\n"); }
    else if(strcasecmp(line, "touch") == 0) { while(*p == ' ') p++; if(*p) fs_touch(p); else prints("Usage: touch <name>\n"); }
    else if(strcasecmp(line, "rm") == 0) { while(*p == ' ') p++; if(*p) fs_rm(p); else prints("Usage: rm <name>\n"); }
    else if(strcasecmp(line, "copy") == 0) {
        while(*p == ' ') p++;
        if(*p) {
            char* src;
            char* dest;
            split_args(p, &src, &dest);
            if (dest) fs_copy(src, dest);
            else prints("Usage: copy <src> <dest>\n");
        } else {
            prints("Usage: copy <src> <dest>\n");
        }
    }
    else if(strcasecmp(line, "writer") == 0) { while(*p == ' ') p++; if(*p) writer_command(p); else prints("Usage: writer <filename>\n"); }
    else if(strcasecmp(line, "ps") == 0) ps_command();
    else if(strcasecmp(line, "kill") == 0) { while(*p == ' ') p++; if(*p) kill_command(p); else prints("Usage: kill <name or pid>\n"); }
    else if(strcasecmp(line, "coreview") == 0) coreview_command();
    else if(strcasecmp(line, "color") == 0) { while(*p == ' ') p++; if(*p) text_color = (*p - '0'); else prints("Usage: color <0-7>\n"); }
        else if(strcasecmp(line, "colorf") == 0) { while(*p == ' ') p++; if(*p) text_color = (*p - '0') | (text_color & 0xF0); else prints("Usage: colorf <0-7>\n"); }
    else if(strcasecmp(line, "install") == 0) { 
        while(*p == ' ') p++; 
        if(*p == '0') install_virtual();
        else if(*p == '1') install_disk();
        else prints("Usage: install <0=virtual, 1=disk>\n"); 
    }
    else if(strcasecmp(line, "config") == 0) config_command();
    else if(strcasecmp(line, "cpu") == 0) cpu_command();
    else if(strcasecmp(line, "date") == 0) date_command();
    else if(strcasecmp(line, "biosver") == 0) biosver_command();
    else if(strcasecmp(line, "calc") == 0) { while(*p == ' ') p++; calc_command(p); }
    else if(strcasecmp(line, "time") == 0) time_command();
    else if(strcasecmp(line, "size") == 0) { while(*p == ' ') p++; if(*p) fs_size(p); else prints("Usage: size <filename>\n"); }
    else if(strcasecmp(line, "osver") == 0) osver_command();
    else if(strcasecmp(line, "history") == 0) history_command();
    else if(strcasecmp(line, "watch") == 0) watch_command();
    else {
        prints("Command not found: ");
        prints(line);
        newline();
    }
    
    if(saved) *p = saved;
}

/* Memory dump command */
void memory_command() {
    prints("Memory Dump (0x1000 - 0x10FF):\n");
    for (u32 addr = 0x1000; addr < 0x1100; addr += 16) {
        char addr_str[10];
        itoa(addr, addr_str, 16);
        prints("0x");
        for (int i = strlen(addr_str); i < 4; i++) prints("0");
        prints(addr_str);
        prints(": ");
        
        for (int i = 0; i < 16; i++) {
            unsigned char val = *(unsigned char*)(addr + i);
            char val_str[3];
            itoa(val, val_str, 16);
            if (val < 0x10) prints("0");
            prints(val_str);
            prints(" ");
        }
        prints(" |");
        for (int i = 0; i < 16; i++) {
            unsigned char val = *(unsigned char*)(addr + i);
            if (val >= 32 && val <= 126) putchar(val);
            else putchar('.');
        }
        prints("|\n");
    }
}

/* Kernel main */
void _start() {
    text_color = 0x07;
    clear_screen();
    
    init_processes();
    fs_init();
    
    prints("WexOS TinyShell v0.6\n");
    prints("Type 'help' for commands\n\n");
    
    char cmd_buf[128];
    int cmd_idx = 0;
    int history_pos = -1;
    
    while(1) {
        prints("> ");
        
        while(1) {
            cursor_row = cursor_row;
            cursor_col = cursor_col;
            
            char c = getch_with_arrows();
            
            if(c == '\n') {
                cmd_buf[cmd_idx] = '\0';
                newline();
                run_command(cmd_buf);
                cmd_idx = 0;
                history_pos = -1;
                break;
            } else if(c == '\b') {
                if(cmd_idx > 0) {
                    cmd_idx--;
                    cmd_buf[cmd_idx] = '\0';
                    cursor_col--;
                    putchar(' ');
                    cursor_col--;
                }
            } else if(c == 'U') {
                if(history_pos < history_count - 1) {
                    history_pos++;
                    strcpy(cmd_buf, command_history[history_count - 1 - history_pos]);
                    cmd_idx = strlen(cmd_buf);
                    
                    cursor_row = cursor_row;
                    cursor_col = cursor_col - (cmd_idx + 2);
                    for(int i = 0; i < COLS; i++) putchar(' ');
                    cursor_col = cursor_col - (cmd_idx + 2);
                    prints("> ");
                    prints(cmd_buf);
                }
            } else if(c == 'D') {
                if(history_pos > 0) {
                    history_pos--;
                    strcpy(cmd_buf, command_history[history_count - 1 - history_pos]);
                    cmd_idx = strlen(cmd_buf);
                    
                    cursor_row = cursor_row;
                    cursor_col = cursor_col - (cmd_idx + 2);
                    for(int i = 0; i < COLS; i++) putchar(' ');
                    cursor_col = cursor_col - (cmd_idx + 2);
                    prints("> ");
                    prints(cmd_buf);
                } else if(history_pos == 0) {
                    history_pos = -1;
                    cmd_idx = 0;
                    cmd_buf[0] = '\0';
                    
                    cursor_row = cursor_row;
                    cursor_col = cursor_col - (cmd_idx + 2);
                    for(int i = 0; i < COLS; i++) putchar(' ');
                    cursor_col = cursor_col - (cmd_idx + 2);
                    prints("> ");
                }
            } else if(c >= 32 && c <= 126 && cmd_idx < 127) {
                cmd_buf[cmd_idx] = c;
                cmd_idx++;
                cmd_buf[cmd_idx] = '\0';
                putchar(c);
            }
        }
    }
}
