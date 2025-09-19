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

typedef struct { 
    char name[MAX_PATH];
    int is_dir; 
    char content[256]; 
    u32 next_sector;
    u32 size;
} FSNode;

/* Function prototypes */
void itoa(int value, char* str, int base);
void putchar(char ch);
char keyboard_getchar();
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
FSNode* fs_find_file(const char* name);
void fs_copy(const char* src_name, const char* dest_name);
void fs_size(const char* name);
void fs_format(void);
void fs_check_integrity(void);
void fsck_command(void);
void fs_cat(const char* filename);

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

void fs_check_integrity(void) {
    prints("Checking filesystem integrity...\n");
    prints("Filesystem: WexFS\n");
    prints("Version: 1.0\n");
    prints("======================================\n");
    
    int errors_found = 0;
    int warnings_found = 0;
    
    prints("Phase 1: Checking inodes...\n");
    for (int i = 0; i < fs_count; i++) {
        if (strlen(fs_cache[i].name) == 0) {
            prints("ERROR: Empty filename at index ");
            char idx_str[10];
            itoa(i, idx_str, 10);
            prints(idx_str);
            newline();
            errors_found++;
        }
        
        if (fs_cache[i].name[0] != '/') {
            prints("ERROR: Filename doesn't start with '/': ");
            prints(fs_cache[i].name);
            newline();
            errors_found++;
        }
    }
    
    prints("Phase 2: Checking for duplicates...\n");
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
    
    prints("Phase 3: Checking directory tree...\n");
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, "/") != 0) {
            char parent_path[MAX_PATH];
            strcpy(parent_path, fs_cache[i].name);
            
            char* last_slash = strrchr(parent_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                if (strlen(parent_path) == 0) {
                    strcpy(parent_path, "/");
                }
                
                int parent_found = 0;
                for (int j = 0; j < fs_count; j++) {
                    if (strcmp(fs_cache[j].name, parent_path) == 0 && fs_cache[j].is_dir) {
                        parent_found = 1;
                        break;
                    }
                }
                
                if (!parent_found) {
                    prints("ERROR: Orphaned file/directory - parent not found: ");
                    prints(fs_cache[i].name);
                    newline();
                    errors_found++;
                }
            }
        }
    }
    
    prints("Phase 4: Checking file sizes...\n");
    for (int i = 0; i < fs_count; i++) {
        if (!fs_cache[i].is_dir) {
            if (fs_cache[i].size > sizeof(fs_cache[i].content)) {
                prints("WARNING: File size exceeds content buffer: ");
                prints(fs_cache[i].name);
                newline();
                warnings_found++;
            }
            
            if (strlen(fs_cache[i].content) != fs_cache[i].size) {
                prints("WARNING: Content length doesn't match size field: ");
                prints(fs_cache[i].name);
                newline();
                warnings_found++;
            }
        }
    }
    
    prints("Phase 5: Checking filesystem limits...\n");
    if (fs_count >= MAX_FILES) {
        prints("WARNING: Filesystem at maximum capacity (");
        char max_str[10];
        itoa(MAX_FILES, max_str, 10);
        prints(max_str);
        prints(" files)\n");
        warnings_found++;
    }
    
    prints("Phase 6: Generating statistics...\n");
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
        }
    }
}

/* System commands */
void reboot_system() {
    prints("Rebooting...\n");
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}

/* Command help */
void show_help() {
    const char* commands[] = {
        "help",     "reboot",   "clear",    "ls",       "cd",       
        "mkdir",    "rm",       "touch",    "copy",     "ps",       
        "kill",     "format",   "fsck",     "cat",      "size",     
        NULL
    };
    
    prints("Available recovery commands:\n");
    prints("===========================\n\n");
    
    for (int i = 0; commands[i] != NULL; i++) {
        prints(commands[i]);
        if (commands[i + 1] != NULL) {
            prints(", ");
        }
        if (i % 5 == 4) newline();
    }
    
    newline();
    prints("\nThese are recovery mode commands only.\n");
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

/* Processes simulation */
typedef struct {
    char name[32];
    int pid;
} Process;

Process processes[10];
int process_count = 0;

void init_processes() {
    strcpy(processes[0].name, "RECOVERY.BIN");
    processes[0].pid = 1;
	strcpy(processes[0].name, "SHELL.BIN");
    processes[0].pid = 2;
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

/* Command parser */
void run_command(char* line) {
    trim_whitespace(line);
    if(*line == 0) return;
    
    char* p = line;
    while(*p && *p != ' ') p++;
    char saved = 0;
    if(*p) { saved = *p; *p = 0; p++; }
    
    if(strcmp(line, "help") == 0) show_help();
    else if(strcmp(line, "reboot") == 0) reboot_system();
    else if(strcmp(line, "clear") == 0) clear_screen();
    else if(strcmp(line, "ls") == 0) fs_ls();
    else if(strcmp(line, "format") == 0) fs_format();
    else if(strcmp(line, "fsck") == 0) fsck_command();
    else if(strcmp(line, "cd") == 0) { while(*p == ' ') p++; if(*p) fs_cd(p); else prints("Usage: cd <directory>\n"); }
    else if(strcmp(line, "mkdir") == 0) { while(*p == ' ') p++; if(*p) fs_mkdir(p); else prints("Usage: mkdir <name>\n"); }
    else if(strcmp(line, "touch") == 0) { while(*p == ' ') p++; if(*p) fs_touch(p); else prints("Usage: touch <name>\n"); }
    else if(strcmp(line, "rm") == 0) { while(*p == ' ') p++; if(*p) fs_rm(p); else prints("Usage: rm <name>\n"); }
    else if(strcmp(line, "cat") == 0) { while(*p == ' ') p++; if(*p) fs_cat(p); else prints("Usage: cat <filename>\n"); }
    else if(strcmp(line, "copy") == 0) {
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
    else if(strcmp(line, "ps") == 0) ps_command();
    else if(strcmp(line, "kill") == 0) { while(*p == ' ') p++; if(*p) kill_command(p); else prints("Usage: kill <name or pid>\n"); }
    else if(strcmp(line, "size") == 0) { while(*p == ' ') p++; if(*p) fs_size(p); else prints("Usage: size <filename>\n"); }
    else {
        prints("Command not found: ");
        prints(line);
        newline();
    }
    
    if(saved) *p = saved;
}

/* Kernel main */
void _start() {
    text_color = 0x07;
    clear_screen();

    init_processes();
    fs_init();

    prints("WexOS Recovery Mode v1.0\n");
    prints("Type 'help' for available commands\n\n");

    char cmd_buf[128];
    int cmd_idx = 0;

    while (1) {
        prints("ROOT> ");

        while (1) {
            char c = keyboard_getchar();

            if (c == '\n') {
                cmd_buf[cmd_idx] = '\0';
                newline();
                run_command(cmd_buf);
                cmd_idx = 0;
                break;
            } else if (c == '\b') {
                if (cmd_idx > 0) {
                    cmd_idx--;
                    cmd_buf[cmd_idx] = '\0';
                    cursor_col--;
                    putchar(' ');
                    cursor_col--;
                }
            } else if (c >= 32 && c <= 126 && cmd_idx < 127) {
                cmd_buf[cmd_idx] = c;
                cmd_idx++;
                cmd_buf[cmd_idx] = '\0';
                putchar(c);
            }
        }
    }
}
