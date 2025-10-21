#include <stdint.h>

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

#define BLACK 0x000000
#define WHITE 0xFFFFFF
#define RED 0xFF0000
#define BLUE 0x0000FF

#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_ENTER 0x1C

/* Function prototypes */
void itoa(int value, char* str, int base);
void putchar(char ch);
char keyboard_getchar();
void reboot_system();
void shutdown_system();
void fill_screen(int color);
void draw_rect(int x, int y, int w, int h, int color);
void draw_text(int x, int y, char* text, int color);
unsigned char get_key();
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
void fs_copy(const char* src_name, const char* dest_name);
void fs_size(const char* name);
void fs_format(void);
void fs_check_integrity(void);
void fsck_command(void);
void fs_cat(const char* filename);
void run_command(char* line);
void trim_whitespace(char* str);
void show_recovery_menu(void);

/* Command history */
int history_count = 0;
int history_index = 0;

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

typedef struct { 
    char name[MAX_PATH];
    int is_dir; 
    char content[4096];  // Увеличил до 4096 байт
    u32 next_sector;
    u32 size;
} FSNode;

FSNode fs_cache[MAX_FILES];
int fs_count = 0;
char current_dir[MAX_PATH] = "/";
int fs_dirty = 0;

/* Command history */
char command_history[MAX_HISTORY][128];

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

void memset(void* ptr, int value, int num) {
    unsigned char* p = (unsigned char*)ptr;
    for (int i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
}

/* Filesystem functions */
void fs_load_from_disk() {
    u8 sector_buffer[SECTOR_SIZE];
    u32 sector = FS_SECTOR_START;
    fs_count = 0;
    fs_dirty = 0;

    #define SECTORS_PER_NODE 11

    while (sector != 0 && fs_count < MAX_FILES) {
        u8* node_data = (u8*)&fs_cache[fs_count];
        
        for (int j = 0; j < SECTORS_PER_NODE; j++) {
            ata_read_sector(sector + j, sector_buffer);
            int offset = j * SECTOR_SIZE;
            if (offset < sizeof(FSNode)) {
                memcpy(node_data + offset, sector_buffer, SECTOR_SIZE);
            }
        }
        
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

    // Теперь структура FSNode занимает примерно 4096 + 1024 + 4 + 4 = 5128 байт
    // 5128 / 512 = 10.01 → 11 секторов на узел
    #define SECTORS_PER_NODE 11

    for (int i = 0; i < fs_count; i++) {
        u8* node_data = (u8*)&fs_cache[i];
        
        for (int j = 0; j < SECTORS_PER_NODE; j++) {
            int offset = j * SECTOR_SIZE;
            if (offset < sizeof(FSNode)) {
                memcpy(sector_buffer, node_data + offset, SECTOR_SIZE);
            } else {
                memset(sector_buffer, 0, SECTOR_SIZE);
            }
            ata_write_sector(sector + j, sector_buffer);
        }
        
        sector = (i == fs_count - 1) ? 0 : FS_SECTOR_START + (i + 1) * SECTORS_PER_NODE;
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
    fs_cache[fs_cache[fs_count].content[0] = '\0'];
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

int folder_size(const char* folder_path) {
    int total = 0;
    char path_with_slash[MAX_PATH];
    strcpy(path_with_slash, folder_path);
    if (folder_path[strlen(folder_path) - 1] != '/') {
        strcat(path_with_slash, "/");
    }

    for (int i = 0; i < fs_count; i++) {
        FSNode* node = &fs_cache[i];
        if (!node->is_dir && strstr(node->name, path_with_slash) == node->name) {
            total += node->size;
        }
    }
    return total;
}

void fs_size(const char* name) {
    FSNode* node = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, name) == 0) {
            node = &fs_cache[i];
            break;
        }
    }

    if (!node) {
        prints("Error: File or folder not found: ");
        prints(name);
        newline();
        return;
    }

    int size;
    if (node->is_dir) {
        size = folder_size(node->name);
        prints("Folder size: ");
    } else {
        size = node->size;
        prints("File size: ");
    }

    char size_str[16];
    itoa(size, size_str, 10);
    prints(size_str);
    prints(" Bytes\n");
}

void find_command(const char* pattern) {
    prints("Searching for: ");
    prints(pattern);
    newline();
    for(int i = 0; i < fs_count; i++) {
        if(strstr(fs_cache[i].name, pattern) != NULL) {
            prints(fs_cache[i].name);
            if(fs_cache[i].is_dir) prints("/");
            newline();
        }
    }
}

void pwd_command() { 
    prints(current_dir); 
    newline(); 
}

void fs_format(void) {
    prints("WARNING: This will erase ALL files and directories!\n");
    prints("Are you sure you want to continue? (y/N): ");
    
    char confirm = keyboard_getchar();
    putchar(confirm);
    newline();
    
    if (confirm == 'y' || confirm == 'Y') {
        prints("Formatting filesystem...\n");
        
        fs_count = 1;
        strcpy(fs_cache[0].name, "/");
        fs_cache[0].is_dir = 1;
        fs_cache[0].content[0] = '\0';
        fs_cache[0].next_sector = 0;
        fs_cache[0].size = 0;
        
        strcpy(current_dir, "/");
        
        fs_mark_dirty();
        fs_save_to_disk();
        
        prints("Filesystem formatted successfully.\n");
    } else {
        prints("Format cancelled.\n");
    }
}

/* Function implementations */
void fs_check_integrity(void) {
    prints("Checking filesystem integrity...\n");
    prints("Filesystem: WexFS\n");
    prints("Version: 1.0\n");
    prints("======================================\n");
    
    int errors_found = 0;
    int warnings_found = 0;
    
    // Проверка на дубликаты
    prints("Phase 1: Checking for duplicates...\n");
    for (int i = 0; i < fs_count; i++) {
        for (int j = i + 1; j < fs_count; j++) {
            if (strcmp(fs_cache[i].name, fs_cache[j].name) == 0) {
                prints("ERROR: Duplicate filename: ");
                prints(fs_cache[i].name);
                newline();
                errors_found++;
            }
        }
    }
    
    // Проверка размера контента
    prints("Phase 2: Checking file sizes...\n");
    for (int i = 0; i < fs_count; i++) {
        if (!fs_cache[i].is_dir) {
            if (fs_cache[i].size > sizeof(fs_cache[i].content)) {
                prints("ERROR: File size exceeds content buffer: ");
                prints(fs_cache[i].name);
                newline();
                prints("  File size: ");
                char size_str[20];
                itoa(fs_cache[i].size, size_str, 10);
                prints(size_str);
                prints(", Max allowed: ");
                itoa(sizeof(fs_cache[i].content), size_str, 10);
                prints(size_str);
                newline();
                errors_found++;
            }
        }
    }
    
    // Проверка максимального количества файлов
    prints("Phase 3: Checking filesystem limits...\n");
    if (fs_count >= MAX_FILES) {
        prints("WARNING: Filesystem at maximum capacity (");
        char max_str[10];
        itoa(MAX_FILES, max_str, 10);
        prints(max_str);
        prints(" files)\n");
        warnings_found++;
    }
    
    // Статистика
    prints("Phase 4: Generating statistics...\n");
    int total_files = 0;
    int total_dirs = 0;
    
    for (int i = 0; i < fs_count; i++) {
        if (fs_cache[i].is_dir) {
            total_dirs++;
        } else {
            total_files++;
        }
    }
    
    prints("======================================\n");
    prints("Filesystem check completed.\n");
    
    char buf[20];
    itoa(total_files, buf, 10);
    prints("Files: "); prints(buf); newline();
    itoa(total_dirs, buf, 10);
    prints("Directories: "); prints(buf); newline();
    itoa(fs_count, buf, 10);
    prints("Total objects: "); prints(buf); newline();
    itoa(MAX_FILES - fs_count, buf, 10);
    prints("Free slots: "); prints(buf); newline();
    
    if (errors_found > 0) {
        itoa(errors_found, buf, 10);
        prints("Errors found: "); prints(buf); newline();
        prints("Run 'format' to fix filesystem errors.\n");
    } else {
        prints("No errors found.\n");
    }
    
    if (warnings_found > 0) {
        itoa(warnings_found, buf, 10);
        prints("Warnings: "); prints(buf); newline();
    }
    
    prints("Filesystem is ");
    if (errors_found == 0) {
        prints("OK");
    } else {
        prints("CORRUPTED");
    }
    prints(".\n");
}
void fsck_command(void) {
    prints("Filesystem Consistency Check\n");
    prints("============================\n");
    prints("This utility will check WexFS filesystem for errors\n");
    prints("and report any inconsistencies.\n\n");
    
    prints("Continue? (y/N): ");
    
    char confirm = keyboard_getchar();
    putchar(confirm);
    newline();
    
    if (confirm == 'y' || confirm == 'Y') {
        fs_check_integrity();
    } else {
        prints("Operation cancelled.\n");
    }
}

void fs_cat(const char* filename) {
    if (filename == NULL || strlen(filename) == 0) {
        prints("Usage: cat <filename>\n");
        return;
    }

    FSNode* file = fs_find_file(filename);
    if (!file) {
        prints("Error: File not found: ");
        prints(filename);
        newline();
        return;
    }

    if (file->is_dir) {
        prints("Error: '");
        prints(filename);
        prints("' is a directory\n");
        return;
    }

    if (file->size > 0) {
        prints(file->content);
        newline();
    } else {
        prints("File is empty\n");
    }
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
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outw(0x5304, 0x0000);
    outw(0x5301, 0x0000);
    outw(0x5308, 0x0001);
    outw(0x5307, 0x0003);
    for(;;) {
        __asm__ volatile("hlt; cli");
    }
}

/* Graphics functions for recovery mode */
void fill_screen(int color) {
    for(int r = 0; r < ROWS; r++) {
        for(int c = 0; c < COLS; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (color << 8));
        }
    }
}

void draw_rect(int x, int y, int w, int h, int color) {
    for(int r = y; r < y + h; r++) {
        for(int c = x; c < x + w; c++) {
            if(r >= 0 && r < ROWS && c >= 0 && c < COLS) {
                VGA[r * COLS + c] = (unsigned short)(' ' | (color << 8));
            }
        }
    }
}

void draw_text(int x, int y, char* text, int color) {
    unsigned char old_color = text_color;
    text_color = color;
    cursor_row = y;
    cursor_col = x;
    prints(text);
    text_color = old_color;
}

unsigned char get_key() {
    while (!(inb(0x64) & 0x01));
    return inb(0x60);
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

void recovery_pass() {
fs_rm("SystemRoot/config/pass.cfg");
prints("The password has been deleted! Or absent.");
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

void info_sys() {
	unsigned char old_color = text_color;
    text_color = 0x0A;
	
prints("Detected kernel drivers:\n");
text_color =  0x04;
prints("RTC - TIME Driver\n");
prints("ATA Disk Driver\n");
text_color =  0x06;
prints("VGA Video Driver\n");
text_color =  0x06;
prints("PS/2 Keyboard Driver\n");
prints("WexFS Driver\n");
prints("Memory Management Driver\n");
text_color =  0x04;
prints("System Control\n");
prints("Multiboot Driver\n");
text_color =  0x0A;
prints("Interrupt handling Driver\n");
text_color =  0x04;
prints("Port I/O\n");
text_color =  0x0A;
prints("Loading Screen Service Driver\n");
prints("Detected User drivers:\n");
prints("AutoRun Service\n");
text_color =  0x04;
prints("Text Rendering Engine\n");
prints("Installation Driver\n");
text_color = old_color;
}

/* Command help for recovery mode */
void show_help() {
    unsigned char old_color = text_color;
    text_color = 0x0A;
    
    const char* commands[] = {
        "help",     "reboot",     "shutdown", "clear",
        "ls",       "cd",         "mkdir",    "rm",       
        "touch",    "copy",       "cat",      "fsck",
        "format",   "size",       "history",  "exit",
        "writer",   "removepass", "drivers",  "pwd",
        "find", NULL
    };
    
    prints("Recovery Mode Commands:\n");
    prints("======================\n\n");
    
    int total_commands = 0;
    while (commands[total_commands] != NULL) {
        total_commands++;
    }
    
    int commands_per_column = (total_commands + 1) / 2;
    int col_width = 15;
    
    for (int i = 0; i < commands_per_column; i++) {
        cursor_col = 2;
        prints(commands[i]);
        
        int spaces = col_width - strlen(commands[i]);
        for (int s = 0; s < spaces; s++) {
            putchar(' ');
        }
        
        if (i + commands_per_column < total_commands) {
            prints(commands[i + commands_per_column]);
        }
        
        newline();
    }
    
    newline();
    prints("These are all commands for recovery mode.\n");
    
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

/* Writer text editor */
void writer_command(const char* filename) {
    FSNode* file = fs_find_file(filename);
    if (!file) {
        prints("Error: File not found: ");
        prints(filename);
        newline();
        return;
    }
    
    char content[4096];  // Увеличили буфер до 4096
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
            if (cursor_row >= ROWS - 1) {
                // Прокрутка вниз если достигли конца экрана
                cursor_row = ROWS - 2;
                cursor_col = 2;
                prints("... [CONTINUED] ...");
                break;
            }
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
                if (content_len < 4095) {  // Обновили лимит
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
                            if (cursor_row >= ROWS - 1) {
                                cursor_row = ROWS - 2;
                                cursor_col = 2;
                                prints("... [CONTINUED] ...");
                                break;
                            }
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
                            if (cursor_row >= ROWS - 1) {
                                cursor_row = ROWS - 2;
                                cursor_col = 2;
                                prints("... [CONTINUED] ...");
                                break;
                            }
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
                if (c >= 32 && c <= 126 && content_len < 4095) {  // Обновили лимит
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
                            if (cursor_row >= ROWS - 1) {
                                cursor_row = ROWS - 2;
                                cursor_col = 2;
                                prints("... [CONTINUED] ...");
                                break;
                            }
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
        if (content_len <= sizeof(file->content)) {
            strcpy(file->content, content);
            file->size = content_len;
            fs_mark_dirty();
            fs_save_to_disk();
            prints("\nFile saved: ");
            prints(filename);
            
            char size_str[20];
            itoa(content_len, size_str, 10);
            prints(" (");
            prints(size_str);
            prints("/4096 bytes)");
            newline();
        } else {
            prints("\nError: File content too large\n");
        }
    }
    
    text_color = old_color;
    clear_screen();
}

/* Command parser for recovery mode */
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
    else if(strcasecmp(line, "reboot") == 0) reboot_system();
    else if(strcasecmp(line, "shutdown") == 0) shutdown_system();
    else if(strcasecmp(line, "clear") == 0) clear_screen();
    else if(strcasecmp(line, "ls") == 0) fs_ls();
    else if(strcasecmp(line, "format") == 0) fs_format();
    else if(strcasecmp(line, "fsck") == 0) fsck_command();
	else if(strcasecmp(line, "drivers") == 0) info_sys();
	else if(strcasecmp(line, "removepass") == 0) recovery_pass();
	else if(strcasecmp(line, "writer") == 0) { while(*p == ' ') p++; if(*p) writer_command(p); else prints("Usage: writer <filename>\n"); }
    else if(strcasecmp(line, "cd") == 0) { while(*p == ' ') p++; if(*p) fs_cd(p); else prints("Usage: cd <directory>\n"); }
    else if(strcasecmp(line, "mkdir") == 0) { while(*p == ' ') p++; if(*p) fs_mkdir(p); else prints("Usage: mkdir <name>\n"); }
    else if(strcasecmp(line, "touch") == 0) { while(*p == ' ') p++; if(*p) fs_touch(p); else prints("Usage: touch <name>\n"); }
    else if(strcasecmp(line, "rm") == 0) { while(*p == ' ') p++; if(*p) fs_rm(p); else prints("Usage: rm <name>\n"); }
    else if(strcasecmp(line, "cat") == 0) { while(*p == ' ') p++; if(*p) fs_cat(p); else prints("Usage: cat <filename>\n"); }
	else if(strcasecmp(line, "pwd") == 0) {
    prints(current_dir);
    newline();
}
else if(strcasecmp(line, "find") == 0) {
    while(*p == ' ') p++;
    if(*p) find_command(p);
    else prints("Usage: find <pattern>\n");
}
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
    else if(strcasecmp(line, "size") == 0) { while(*p == ' ') p++; if(*p) fs_size(p); else prints("Usage: size <filename>\n"); }
    else if(strcasecmp(line, "history") == 0) history_command();
    else if(strcasecmp(line, "exit") == 0) { prints("Use 'reboot' or 'shutdown' to exit\n"); }
    else {
        prints("Command not found: ");
        prints(line);
        prints("\nType 'help' for available commands\n");
    }
    
    if(saved) *p = saved;
}

/* Kernel main for recovery mode */
void _start() {
    text_color = 0x07;
    clear_screen();

    // Инициализация файловой системы
    fs_init();

    prints("WexOS Recovery Mode v0.6\n");
    prints("===========================\n");
    prints("System started in recovery mode\n\n");

    // Если вышли из меню, переходим в командную строку
    prints("Recovery Command Line\n");
    prints("Type 'help' for commands \n");

    char cmd_buf[128];
    int cmd_idx = 0;
    int history_pos = -1;

    while (1) {
        prints("Recovery> ");

        while (1) {
            cursor_row = cursor_row;
            cursor_col = cursor_col;

            char c = getch_with_arrows();

            if (c == '\n') {
                cmd_buf[cmd_idx] = '\0';
                newline();
                
                if (cmd_idx > 0) {
                    run_command(cmd_buf);
                }
                
                cmd_idx = 0;
                history_pos = -1;
                break;
            } else if (c == '\b') {
                if (cmd_idx > 0) {
                    cmd_idx--;
                    cmd_buf[cmd_idx] = '\0';
                    cursor_col--;
                    putchar(' ');
                    cursor_col--;
                }
            } else if (c == 'U') { // стрелка вверх
                if (history_pos < history_count - 1) {
                    history_pos++;
                    strcpy(cmd_buf, command_history[history_count - 1 - history_pos]);
                    cmd_idx = strlen(cmd_buf);

                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("Recovery> ");
                    prints(cmd_buf);
                }
            } else if (c == 'D') { // стрелка вниз
                if (history_pos > 0) {
                    history_pos--;
                    strcpy(cmd_buf, command_history[history_count - 1 - history_pos]);
                    cmd_idx = strlen(cmd_buf);

                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("Recovery> ");
                    prints(cmd_buf);
                } else if (history_pos == 0) {
                    history_pos = -1;
                    cmd_idx = 0;
                    cmd_buf[0] = '\0';

                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("Recovery> ");
                }
            } else if (c >= 32 && c <= 126 && cmd_idx < 127) {
                cmd_buf[cmd_idx] = c;
                cmd_idx++;
                cmd_buf[cmd_idx] = '\0';
                putchar(c);
            } else if (c == 0x01) { // ESC
                cmd_idx = 0;
                cmd_buf[0] = '\0';
                
                cursor_row = cursor_row;
                cursor_col = 0;
                for (int i = 0; i < COLS; i++) {
                    putchar(' ');
                }
                
                cursor_row = cursor_row;
                cursor_col = 0;
                prints("Recovery> ");
            } else if (c == '\t') { // TAB
                if (cmd_idx < 126) {
                    cmd_buf[cmd_idx] = ' ';
                    cmd_idx++;
                    cmd_buf[cmd_idx] = '\0';
                    putchar(' ');
                }
            }
        }
    }
}
