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

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define CURSOR_SPEED 5

#define DARK_BLUE 0x00008B
#define LIGHT_GRAY 0xC0C0C0
#define BLACK 0x000000
#define WHITE 0xFFFFFF

#define LOADING_BG_COLOR 0x00      // Черный
#define LOADING_TEXT_COLOR 0x0F    // Белый на черном
#define LOADING_ACCENT_COLOR 0x0B  // Голубой

#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_ENTER 0x1C

#define EXPLORER_WIDTH COLS
#define EXPLORER_HEIGHT ROWS
#define MAX_VISIBLE_FILES (ROWS - 4)
#define PATH_MAX_DISPLAY 70

#define AUTORUN_FILE "SystemRoot/config/autorun.cfg"
#define AUTORUN_MAX_COMMAND 128


typedef struct {
    char name[MAX_NAME];
    int is_dir;
    u32 size;
} FileEntry;

typedef struct {
    int x, y;
    int width, height;
    char title[MAX_NAME];
    FileEntry files[MAX_FILES];
    int file_count;
    int selected_index;
    int scroll_offset;
    char current_path[MAX_PATH];
} Explorer;

typedef struct { 
    char name[MAX_PATH];
    int is_dir; 
    char content[4096];  // Увеличил до 4096 байт
    u32 next_sector;
    u32 size;
} FSNode;

/* Function prototypes */
void itoa(int value, char* str, int base);
void coreview_command(void);
void putchar(char ch);
char keyboard_getchar();
void reboot_system();
void shutdown_system();
void draw_button_cursor(int button_area_x, int button_area_y, int selected_button);
void draw_button_highlight(int button_area_x, int button_area_y, int selected_button);
void update_buttons_display(int button_area_x, int button_area_y, int selected_button, int focus_on_buttons);
int check_login();
void fill_screen(int color);
void draw_rect(int x, int y, int w, int h, int color);
void draw_text(int x, int y, char* text, int color);
void draw_cursor(int x, int y);            
unsigned char get_key(); // возвращает сканкод нажатой клавиши (PS/2)               
void get_datetime(char *buffer); 
void open_terminal();
void show_loading_screen(void);
void draw_wexos_logo(void);
void draw_loading_animation(int frame, int progress);
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
void fs_format(void);
void fs_check_integrity(void);
void fsck_command(void);
void fs_cat(const char* filename);
void writer_command(const char* filename);
void wexplorer_command(void);
void draw_explorer(Explorer* exp);
void explorer_navigate(Explorer* exp, const char* path);
void explorer_refresh(Explorer* exp);
int explorer_handle_input(Explorer* exp);
void draw_file_list(Explorer* exp);
void draw_status_bar(Explorer* exp);
void autorun_command(const char* arg);
void autorun_execute(void);
void autorun_save_config(const char* command);
void run_command(char* line);
void exit_command(void);
void restore_background(int x, int y);
void trim_whitespace(char* str);

/* Command history */
int history_count = 0;
int history_index = 0;

/* AutoRun configuration */
char autorun_command_buf[AUTORUN_MAX_COMMAND] = {0};
int autorun_enabled = 0;

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
    // ищем узел с точным именем
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
        
        // Сбрасываем файловую систему к начальному состоянию
        fs_count = 1;
        strcpy(fs_cache[0].name, "/");
        fs_cache[0].is_dir = 1;
        fs_cache[0].content[0] = '\0';
        fs_cache[0].next_sector = 0;
        fs_cache[0].size = 0;
        
        // Сбрасываем текущую директорию
        strcpy(current_dir, "/");
        
        // Помечаем как измененную и сохраняем
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

int check_login() {
    FSNode* passfile = fs_find_file("SystemRoot/config/pass.cfg");
    if (!passfile || passfile->size == 0) {
        // Пароль не установлен
        return 1;
    }

    unsigned char old_color = text_color;
    
    // Очищаем экран и рисуем фон с озером и полем
    clear_screen();
    
    // Рисуем небо (верхние 2/3 экрана)
    for (int r = 0; r < ROWS - 4; r++) {
        for (int c = 0; c < COLS; c++) {
            // Градиент неба от светло-голубого к синему
            int sky_color = 0x10 + (r * 3 / (ROWS - 4));
            VGA[r * COLS + c] = (unsigned short)(' ' | (sky_color << 8));
        }
    }
    
    // Рисуем озеро (синяя область над травой)
    int lake_start_row = (ROWS - 4) * 2 / 3;
    int lake_end_row = ROWS - 4;
    for (int r = lake_start_row; r < lake_end_row; r++) {
        for (int c = 0; c < COLS; c++) {
            // Волны на озере - переменная интенсивность синего
            int wave_pattern = (r + c) % 4;
            int water_color = 0x10 + wave_pattern;
            VGA[r * COLS + c] = (unsigned short)(' ' | (water_color << 8));
        }
    }
    
    // Рисуем поле (нижние 4 строки)
    for (int r = ROWS - 4; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            // Трава с текстурой - разные символы для разнообразия
            int grass_pattern = (r + c) % 5;
            char grass_char;
            switch(grass_pattern) {
                case 0: grass_char = '`'; break;
                case 1: grass_char = ','; break;
                case 2: grass_char = '\''; break;
                case 3: grass_char = ' '; break;
                default: grass_char = '.'; break;
            }
            int grass_color = 0x20 + (grass_pattern % 3);
            VGA[r * COLS + c] = (unsigned short)(grass_char | (grass_color << 8));
        }
    }

    // Рисуем кнопки в правом верхнем углу (меньшие и отдельные)
    int button_area_x = COLS - 22;  // Уменьшили ширину области
    int button_area_y = 2;
    
    // Отдельные фоны для каждой кнопки (не сплошной прямоугольник)
    // Фон для кнопки Reboot
    for (int r = button_area_y; r < button_area_y + 2; r++) {
        for (int c = button_area_x + 2; c < button_area_x + 8; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        }
    }
    
    // Фон для кнопки Shutdown
    for (int r = button_area_y; r < button_area_y + 2; r++) {
        for (int c = button_area_x + 12; c < button_area_x + 20; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        }
    }
    
    // Кнопки без скобок с увеличенным расстоянием
    cursor_row = button_area_y;
    cursor_col = button_area_x + 2;
    text_color = 0x70; // Черный на сером
    prints("Reboot");
    
    cursor_row = button_area_y;
    cursor_col = button_area_x + 12;
    prints("Shutdown");
    
    // Курсор для переключения стрелочками
    int selected_button = 0; // 0 - Reboot, 1 - Shutdown
    
    // Рисуем начальное выделение
    draw_button_highlight(button_area_x, button_area_y, selected_button);

    char buffer[64] = {0};
    int buffer_len = 0;
    int cursor_visible = 1;
    int cursor_timer = 0;
    int focus_on_buttons = 1; // 1 - фокус на кнопках, 0 - фокус на поле ввода

    while (1) {
        // Область ввода без рамки
        int area_width = 40;
        int area_height = 6;
        int area_x = (COLS - area_width) / 2;
        int area_y = (ROWS - area_height) / 2;
        
        // Очищаем область ввода (прозрачный фон)
        for (int r = area_y; r < area_y + area_height; r++) {
            for (int c = area_x; c < area_x + area_width; c++) {
                // Сохраняем фон, но делаем немного темнее для контраста
                unsigned short current = VGA[r * COLS + c];
                int bg_color = (current >> 8) & 0xF0;
                if (bg_color > 0x10) {
                    VGA[r * COLS + c] = (unsigned short)(' ' | ((bg_color - 0x10) << 8));
                }
            }
        }
        
        // Заголовок "Welcome"
        cursor_row = area_y + 1;
        cursor_col = area_x + (area_width - 7) / 2;
        text_color = 0x1F; // Синий на белом
        prints("Welcome");
        
        // Подпись "Enter password:"
        cursor_row = area_y + 2;
        cursor_col = area_x + 2;
        text_color = 0x1F;
        prints("Enter password:");
        
        // Область ввода пароля
        int input_x = area_x + 2;
        int input_y = area_y + 3;
        int input_width = area_width - 4;
        
        // Очищаем область ввода (белый фон)
        for (int c = input_x; c < input_x + input_width; c++) {
            VGA[input_y * COLS + c] = (unsigned short)(' ' | (0x0F << 8));
        }
        
        // Отображаем звёздочки вместо пароля
        cursor_row = input_y;
        cursor_col = input_x;
        text_color = 0x0F; // Черный на белом
        for (int i = 0; i < buffer_len; i++) {
            putchar('*');
        }
        
        // Мигающий курсор в поле ввода (только если фокус там)
        cursor_timer++;
        if (cursor_timer > 10) {
            cursor_visible = !cursor_visible;
            cursor_timer = 0;
        }
        
        if (cursor_visible && buffer_len < 63 && !focus_on_buttons) {
            cursor_row = input_y;
            cursor_col = input_x + buffer_len;
            putchar('_');
        }
        
        // Подсказки внизу области
        cursor_row = area_y + area_height - 1;
        cursor_col = area_x + 2;
        text_color = 0x1F;
        if (focus_on_buttons) {
            prints("Use arrows to select, ENTER to activate");
        } else {
            prints("Press ENTER to login, TAB to switch focus");
        }
        
        // Обновляем кнопки с выделением
        update_buttons_display(button_area_x, button_area_y, selected_button, focus_on_buttons);
        
        // Обработка ввода
        unsigned char st = inb(0x64);
        if (st & 1) {
            unsigned char sc = inb(0x60);
            
            if ((sc & 0x80) == 0) { // Только нажатие, не отпускание
                if (sc == 0x1C) { // Enter
                    if (focus_on_buttons) {
                        // Активируем выбранную кнопку
                        if (selected_button == 0) { // Reboot
                            prints("\nRebooting...");
                            reboot_system();
                        } else { // Shutdown
                            prints("\nShutting down...");
                            shutdown_system();
                        }
                    } else {
                        // Обработка входа в систему
                        buffer[buffer_len] = '\0';
                        newline();
                        
                        if (strcmp(buffer, passfile->content) == 0) {
                            // Успешный вход - очищаем экран и возвращаемся
                            text_color = old_color;
                            clear_screen();
                            prints("Login successful!\n");
                            return 1;
                        } else {
                            // Неверный пароль - показываем сообщение об ошибке
                            cursor_row = area_y + 4;
                            cursor_col = area_x + 2;
                            text_color = 0x4F; // Красный фон, белый текст
                            
                            // Очищаем область сообщения
                            for (int c = area_x + 2; c < area_x + area_width - 2; c++) {
                                VGA[(area_y + 4) * COLS + c] = (unsigned short)(' ' | (0x4F << 8));
                            }
                            
                            cursor_row = area_y + 4;
                            cursor_col = area_x + 2;
                            prints("Incorrect password, try again.");
                            
                            // Очищаем буфер
                            buffer_len = 0;
                            buffer[0] = '\0';
                            
                            // Ждём немного перед очисткой сообщения
                            for (volatile int i = 0; i < 1000000; i++);
                        }
                    }
                } else if (sc == 0x0E) { // Backspace
                    if (!focus_on_buttons && buffer_len > 0) {
                        buffer_len--;
                        buffer[buffer_len] = '\0';
                    }
                } else if (sc == 0x0F) { // Tab - переключение фокуса
                    focus_on_buttons = !focus_on_buttons;
                } else if (sc == 0x4B) { // Стрелка влево
                    if (focus_on_buttons) {
                        selected_button = 0;
                    }
                } else if (sc == 0x4D) { // Стрелка вправо
                    if (focus_on_buttons) {
                        selected_button = 1;
                    }
                } else if (sc == 0x48) { // Стрелка вверх
                    focus_on_buttons = 1;
                } else if (sc == 0x50) { // Стрелка вниз
                    focus_on_buttons = 0;
                } else {
                    // Обычные символы (только если фокус на поле ввода)
                    if (!focus_on_buttons) {
                        static const char t[128] = {
                            0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
                            'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
                            'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
                            'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
                        };
                        
                        if (sc < 128 && t[sc] && buffer_len < 63) {
                            char c = t[sc];
                            if (c >= 32 && c <= 126) { // Только печатные символы
                                buffer[buffer_len] = c;
                                buffer_len++;
                                buffer[buffer_len] = '\0';
                            }
                        }
                    }
                }
            }
        }
        
        // Небольшая задержка для плавности
        for (volatile int i = 0; i < 10000; i++);
    }
}

void draw_button_highlight(int button_area_x, int button_area_y, int selected_button) {
    int button_start_x, button_width;
    
    if (selected_button == 0) {
        // Reboot button
        button_start_x = button_area_x + 2;
        button_width = 6; // "Reboot" = 6 символов
    } else {
        // Shutdown button
        button_start_x = button_area_x + 12;
        button_width = 8; // "Shutdown" = 8 символов
    }
    
    // Закрашиваем область кнопки красным фоном (только саму кнопку)
    for (int c = button_start_x; c < button_start_x + button_width; c++) {
        VGA[button_area_y * COLS + c] = (unsigned short)(' ' | (0x47 << 8));
        VGA[(button_area_y + 1) * COLS + c] = (unsigned short)(' ' | (0x47 << 8));
    }
}

void update_buttons_display(int button_area_x, int button_area_y, int selected_button, int focus_on_buttons) {
    // Восстанавливаем фон кнопок (отдельные области для каждой кнопки)
    
    // Фон для кнопки Reboot
    for (int r = button_area_y; r < button_area_y + 2; r++) {
        for (int c = button_area_x + 2; c < button_area_x + 8; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        }
    }
    
    // Фон для кнопки Shutdown
    for (int r = button_area_y; r < button_area_y + 2; r++) {
        for (int c = button_area_x + 12; c < button_area_x + 20; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        }
    }
    
    // Если фокус на кнопках, выделяем выбранную кнопку красным
    if (focus_on_buttons) {
        draw_button_highlight(button_area_x, button_area_y, selected_button);
    }
    
    // Рисуем текст кнопок
    cursor_row = button_area_y;
    
    // Reboot button
    cursor_col = button_area_x + 2;
    if (focus_on_buttons && selected_button == 0) {
        text_color = 0x47; // Белый на красном
    } else {
        text_color = 0x70; // Черный на сером
    }
    prints("Reboot");
    
    // Shutdown button
    cursor_col = button_area_x + 12;
    if (focus_on_buttons && selected_button == 1) {
        text_color = 0x47; // Белый на красном
    } else {
        text_color = 0x70; // Черный на сером
    }
    prints("Shutdown");
}

/* Function implementation */
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

    // Выводим содержимое файла
    if (file->size > 0) {
        prints(file->content);
        newline();
    } else {
        prints("File is empty\n");
    }
}

/* WexExplorer - файловый менеджер */
void explorer_refresh(Explorer* exp) {
    exp->file_count = 0;
    exp->selected_index = 0;
    exp->scroll_offset = 0;
    
    // Добавляем ".." для навигации вверх (кроме корня)
    if (strcmp(exp->current_path, "/") != 0) {
        strcpy(exp->files[exp->file_count].name, "..");
        exp->files[exp->file_count].is_dir = 1;
        exp->files[exp->file_count].size = 0;
        exp->file_count++;
    }
    
    // Временные массивы для сортировки
    FileEntry folders[MAX_FILES];
    FileEntry files[MAX_FILES];
    int folder_count = 0;
    int file_count = 0;
    
    // Сканируем файловую систему
    for (int i = 0; i < fs_count; i++) {
        int is_in_current_dir = 0;
        char relative_path[MAX_NAME] = "";
        
        if (strcmp(exp->current_path, "/") == 0) {
            // Корневая директория
            if (fs_cache[i].name[0] == '/') {
                char* second_slash = strchr(fs_cache[i].name + 1, '/');
                if (second_slash == NULL) {
                    is_in_current_dir = 1;
                    strcpy(relative_path, fs_cache[i].name + 1);
                }
            } else {
                is_in_current_dir = 1;
                strcpy(relative_path, fs_cache[i].name);
            }
        } else {
            // Поддиректории
            char search_path[MAX_PATH];
            strcpy(search_path, exp->current_path);
            if (search_path[strlen(search_path)-1] != '/') {
                strcat(search_path, "/");
            }
            
            if (strstr(fs_cache[i].name, search_path) == fs_cache[i].name) {
                char* name_part = fs_cache[i].name + strlen(search_path);
                if (name_part[0] != '\0') {
                    char* next_slash = strchr(name_part, '/');
                    if (next_slash == NULL) {
                        is_in_current_dir = 1;
                        strcpy(relative_path, name_part);
                    }
                }
            }
        }
        
        if (is_in_current_dir && strlen(relative_path) > 0) {
            int duplicate = 0;
            for (int j = 0; j < exp->file_count; j++) {
                if (strcmp(exp->files[j].name, relative_path) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            
            if (!duplicate) {
                if (fs_cache[i].is_dir) {
                    strcpy(folders[folder_count].name, relative_path);
                    folders[folder_count].is_dir = 1;
                    folders[folder_count].size = fs_cache[i].size;
                    folder_count++;
                } else {
                    strcpy(files[file_count].name, relative_path);
                    files[file_count].is_dir = 0;
                    files[file_count].size = fs_cache[i].size;
                    file_count++;
                }
            }
        }
    }
    
    // Сортируем папки по алфавиту
    for (int i = 0; i < folder_count - 1; i++) {
        for (int j = i + 1; j < folder_count; j++) {
            if (strcmp(folders[i].name, folders[j].name) > 0) {
                FileEntry temp = folders[i];
                folders[i] = folders[j];
                folders[j] = temp;
            }
        }
    }
    
    // Сортируем файлы по алфавиту
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (strcmp(files[i].name, files[j].name) > 0) {
                FileEntry temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }
    
    // Объединяем: сначала папки, потом файлы
    for (int i = 0; i < folder_count && exp->file_count < MAX_FILES; i++) {
        exp->files[exp->file_count] = folders[i];
        exp->file_count++;
    }
    
    for (int i = 0; i < file_count && exp->file_count < MAX_FILES; i++) {
        exp->files[exp->file_count] = files[i];
        exp->file_count++;
    }
}

void draw_file_list(Explorer* exp) {
    int list_height = ROWS - 4;
    int start_y = 2;
    
    for (int i = 0; i < list_height && i + exp->scroll_offset < exp->file_count; i++) {
        int file_idx = i + exp->scroll_offset;
        FileEntry* file = &exp->files[file_idx];
        
        cursor_row = start_y + i;
        cursor_col = 2;
        
        // Выделение
        if (file_idx == exp->selected_index) {
            // Очищаем строку
            for (int c = 0; c < COLS; c++) {
                VGA[cursor_row * COLS + c] = (unsigned short)(' ' | (0x4F << 8));
            }
            text_color = 0x4F;
        } else {
            text_color = 0x1F;
        }
        
        cursor_col = 2;
        
        // Иконка папки/файла
        if (file->is_dir) {
            prints("[+] ");
        } else {
            prints("    ");
        }
        
        // Имя файла
        char display_name[60];
        strcpy(display_name, file->name);
        if (strlen(display_name) > 50) {
            display_name[47] = '.';
            display_name[48] = '.';
            display_name[49] = '.';
            display_name[50] = '\0';
        }
        prints(display_name);
        
        // Размер
        cursor_col = COLS - 25;
        if (file->is_dir) {
            prints("<DIR>");
        } else {
            char size_str[15];
            itoa(file->size, size_str, 10);
            prints(size_str);
            prints(" bytes");
        }
        
        // Тип
        cursor_col = COLS - 10;
        if (file->is_dir) {
            prints("Folder");
        } else {
            char* dot = strrchr(file->name, '.');
            if (dot && dot != file->name) {
                char ext[10];
                strcpy(ext, dot + 1);
                if (strlen(ext) > 6) ext[6] = '\0';
                prints(ext);
            } else {
                prints("File");
            }
        }
    }
}

void draw_status_bar(Explorer* exp) {
    // Статус бар
    for (int c = 0; c < COLS; c++) {
        VGA[(ROWS-2) * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        VGA[(ROWS-1) * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
    }
    
    cursor_row = ROWS - 2;
    cursor_col = 2;
    text_color = 0x70;
    
    // Статистика
    char stat_buf[100];
    strcpy(stat_buf, "Total: ");
    char count_str[10];
    itoa(exp->file_count, count_str, 10);
    strcat(stat_buf, count_str);
    
    // Подсчет папок и файлов
    int dir_count = 0, file_count = 0;
    for (int i = 0; i < exp->file_count; i++) {
        if (exp->files[i].is_dir) dir_count++;
        else file_count++;
    }
    
    strcat(stat_buf, " (");
    itoa(dir_count, count_str, 10);
    strcat(stat_buf, count_str);
    strcat(stat_buf, " folders, ");
    itoa(file_count, count_str, 10);
    strcat(stat_buf, count_str);
    strcat(stat_buf, " files)");
    
    // Выбранный файл
    if (exp->file_count > 0 && exp->selected_index < exp->file_count) {
        strcat(stat_buf, " | Selected: ");
        strcat(stat_buf, exp->files[exp->selected_index].name);
    }
    
    prints(stat_buf);
    
    // Подсказки
    cursor_row = ROWS - 1;
    cursor_col = 2;
    prints("Enter:Open  Up/Dn:Navigate  ESC:Exit  Home/End:Scroll");
}

void draw_explorer(Explorer* exp) {
    unsigned char old_color = text_color;
    
    // Очищаем весь экран
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x1F << 8));
        }
    }
    
    // Верхняя панель
    for (int c = 0; c < COLS; c++) {
        VGA[0 * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
        VGA[1 * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
    }
    
    // Заголовок (обрезаем если слишком длинный)
    char display_title[PATH_MAX_DISPLAY];
    strcpy(display_title, "WexExplorer - ");
    strcat(display_title, exp->current_path);
    
    if (strlen(display_title) > PATH_MAX_DISPLAY) {
        strcpy(display_title, "WexExplorer - ...");
        strcat(display_title, exp->current_path + strlen(exp->current_path) - (PATH_MAX_DISPLAY - 20));
    }
    
    cursor_row = 0;
    cursor_col = (COLS - strlen(display_title)) / 2;
    text_color = 0x70;
    prints(display_title);
    
    // Заголовки колонок
    cursor_row = 1;
    cursor_col = 2;
    text_color = 0x70;
    prints("Name");
    cursor_col = COLS - 25;
    prints("Size");
    cursor_col = COLS - 10;
    prints("Type");
    
    // Список файлов
    draw_file_list(exp);
    
    // Статус бар
    draw_status_bar(exp);
    
    text_color = old_color;
}

int explorer_handle_input(Explorer* exp) {
    int list_height = ROWS - 4;
    
    while (1) {
        unsigned char st = inb(0x64);
        if (st & 1) {
            unsigned char sc = inb(0x60);
            
            if (sc == 0xE0) {
                // Extended key
                while (!(inb(0x64) & 1));
                sc = inb(0x60);
                
                switch (sc) {
                    case 0x48: // Стрелка вверх
                        if (exp->selected_index > 0) {
                            exp->selected_index--;
                            if (exp->selected_index < exp->scroll_offset) {
                                exp->scroll_offset = exp->selected_index;
                            }
                            return 0; // Перерисовать
                        }
                        break;
                        
                    case 0x50: // Стрелка вниз
                        if (exp->selected_index < exp->file_count - 1) {
                            exp->selected_index++;
                            if (exp->selected_index >= exp->scroll_offset + list_height) {
                                exp->scroll_offset = exp->selected_index - list_height + 1;
                            }
                            return 0; // Перерисовать
                        }
                        break;
                        
                    case 0x47: // Home
                        exp->selected_index = 0;
                        exp->scroll_offset = 0;
                        return 0;
                        
                    case 0x4F: // End
                        exp->selected_index = exp->file_count - 1;
                        if (exp->file_count > list_height) {
                            exp->scroll_offset = exp->file_count - list_height;
                        }
                        return 0;
                        
                    case 0x1C: // Enter (extended)
                        return 1; // Открыть
                }
            } else if (sc == 0x1C) { // Обычный Enter
                return 1; // Открыть
            } else if (sc == 0x01) { // ESC
                return 2; // Выход
            }
        }
        
        // Небольшая задержка для плавности
        for (volatile int i = 0; i < 5000; i++);
    }
}

void wexplorer_command(void) {
    Explorer exp;
    exp.x = 0;
    exp.y = 0;
    exp.width = EXPLORER_WIDTH;
    exp.height = EXPLORER_HEIGHT;
    exp.selected_index = 0;
    exp.scroll_offset = 0;
    
    strcpy(exp.current_path, current_dir);
    explorer_refresh(&exp);
    
    int exit_explorer = 0;
    unsigned char old_color = text_color;
    
    while (!exit_explorer) {
        draw_explorer(&exp);
        int action = explorer_handle_input(&exp);
        
        switch (action) {
            case 1: // Enter - открыть файл/папку
                if (exp.file_count > 0 && exp.selected_index < exp.file_count) {
                    FileEntry* selected = &exp.files[exp.selected_index];
                    
                    if (selected->is_dir) {
                        // Переход в папку
                        if (strcmp(selected->name, "..") == 0) {
                            // Назад
                            if (strcmp(exp.current_path, "/") != 0) {
                                char* last_slash = strrchr(exp.current_path, '/');
                                if (last_slash != NULL) {
                                    if (last_slash == exp.current_path) {
                                        strcpy(exp.current_path, "/");
                                    } else {
                                        *last_slash = '\0';
                                        if (strlen(exp.current_path) == 0) {
                                            strcpy(exp.current_path, "/");
                                        }
                                    }
                                }
                            }
                        } else {
                            // Вход в папку
                            char new_path[MAX_PATH];
                            if (strcmp(exp.current_path, "/") == 0) {
                                strcpy(new_path, "/");
                                strcat(new_path, selected->name);
                            } else {
                                strcpy(new_path, exp.current_path);
                                if (new_path[strlen(new_path)-1] != '/') {
                                    strcat(new_path, "/");
                                }
                                strcat(new_path, selected->name);
                            }
                            
                            // Проверяем, что это действительно папка
                            int is_valid_dir = 0;
                            for (int i = 0; i < fs_count; i++) {
                                if (strcmp(fs_cache[i].name, new_path) == 0 && fs_cache[i].is_dir) {
                                    is_valid_dir = 1;
                                    break;
                                }
                            }
                            
                            if (is_valid_dir) {
                                strcpy(exp.current_path, new_path);
                                // Добавляем слэш в конец если его нет
                                if (exp.current_path[strlen(exp.current_path)-1] != '/') {
                                    strcat(exp.current_path, "/");
                                }
                            }
                        }
                        
                        explorer_refresh(&exp);
                    } else {
                        // Открыть файл в Writer
                        char full_path[MAX_PATH];
                        if (strcmp(exp.current_path, "/") == 0) {
                            strcpy(full_path, selected->name);
                        } else {
                            strcpy(full_path, exp.current_path);
                            strcat(full_path, selected->name);
                        }
                        
                        // Выходим из explorer и открываем файл
                        exit_explorer = 1;
                        text_color = old_color;
                        clear_screen();
                        prints("Opening file in Writer: ");
                        prints(full_path);
                        newline();
                        writer_command(full_path);
                        return;
                    }
                }
                break;
                
            case 2: // ESC - выход
                exit_explorer = 1;
                break;
        }
    }
    
    text_color = old_color;
    clear_screen();
    prints("WexExplorer closed.\n> ");
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

/* Get datetime for desktop */
void get_datetime(char *buffer) {
    // Ждем завершения обновления RTC
    while (1) {
        outb(0x70, 0x0A);
        if (!(inb(0x71) & 0x80)) break;
    }

    // Читаем значения из CMOS
    unsigned char second = rtc_read(0x00);
    unsigned char minute = rtc_read(0x02);
    unsigned char hour   = rtc_read(0x04);
    unsigned char day    = rtc_read(0x07);
    unsigned char month  = rtc_read(0x08);
    unsigned char year   = rtc_read(0x09);

    // Конвертируем из BCD в binary
    second = bcd_to_bin(second);
    minute = bcd_to_bin(minute);
    hour   = bcd_to_bin(hour);
    day    = bcd_to_bin(day);
    month  = bcd_to_bin(month);
    year   = bcd_to_bin(year);

    // Форматируем в строку DD.MM.YY HH:MM:SS
    buffer[0] = '0' + (day / 10);    buffer[1] = '0' + (day % 10);
    buffer[2] = '.';
    buffer[3] = '0' + (month / 10);  buffer[4] = '0' + (month % 10);
    buffer[5] = '.';
    buffer[6] = '0' + (year / 10);   buffer[7] = '0' + (year % 10);
    buffer[8] = ' ';
    buffer[9] = '0' + (hour / 10);   buffer[10] = '0' + (hour % 10);
    buffer[11] = ':';
    buffer[12] = '0' + (minute / 10); buffer[13] = '0' + (minute % 10);
    buffer[14] = ':';
    buffer[15] = '0' + (second / 10); buffer[16] = '0' + (second % 10);
    buffer[17] = '\0';
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
    
    // Попытка ACPI выключения через порт 0x604
    outw(0x604, 0x2000);
    
    // Альтернативный ACPI метод
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    
    // Если ACPI не работает, пробуем APM
    outw(0x5304, 0x0000);  // Отключить APM
    outw(0x5301, 0x0000);  // Установить устройство
    outw(0x5308, 0x0001);  // Установить версию
    outw(0x5307, 0x0003);  // Выключить систему
    
    // Запасной вариант
    for(;;) {
        __asm__ volatile("hlt; cli");
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

void cmd_desktop() {
    // Сохраняем старый цвет текста
    unsigned char old_color = text_color;

    // Устанавливаем цвет для рабочего стола
    text_color = 0x1F;
    clear_screen();

    // ------ Рисуем статический фон ------
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (r > ROWS - 5) {
                // Трава (нижние 5 строк)
                int grass_pattern = (r + c) % 4;
                char grass_char = (grass_pattern == 0) ? '`' : ' ';
                int grass_color = 0x20 + grass_pattern;
                VGA[r * COLS + c] = (unsigned short)(grass_char | (grass_color << 8));
            } else {
                // Небо
                int sky_color = 0x10 + (r * 2 / ROWS);
                VGA[r * COLS + c] = (unsigned short)(' ' | (sky_color << 8));
            }
        }
    }

    // ------ Добавляем облака ------
    // Облако 1
    int cloud1_y = 3;
    int cloud1_x = 10;
    for (int i = 0; i < 7; i++) {
        VGA[cloud1_y * COLS + cloud1_x + i] = (unsigned short)(' ' | (0x70 << 8));
    }
    VGA[(cloud1_y-1) * COLS + cloud1_x + 2] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud1_y-1) * COLS + cloud1_x + 3] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud1_y-1) * COLS + cloud1_x + 4] = (unsigned short)(' ' | (0x70 << 8));

    // Облако 2
    int cloud2_y = 6;
    int cloud2_x = 40;
    for (int i = 0; i < 5; i++) {
        VGA[cloud2_y * COLS + cloud2_x + i] = (unsigned short)(' ' | (0x70 << 8));
    }
    VGA[(cloud2_y-1) * COLS + cloud2_x + 1] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud2_y-1) * COLS + cloud2_x + 2] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud2_y-1) * COLS + cloud2_x + 3] = (unsigned short)(' ' | (0x70 << 8));

    // Облако 3
    int cloud3_y = 4;
    int cloud3_x = 60;
    for (int i = 0; i < 6; i++) {
        VGA[cloud3_y * COLS + cloud3_x + i] = (unsigned short)(' ' | (0x70 << 8));
    }
    VGA[(cloud3_y-1) * COLS + cloud3_x + 1] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud3_y-1) * COLS + cloud3_x + 2] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud3_y-1) * COLS + cloud3_x + 3] = (unsigned short)(' ' | (0x70 << 8));
    VGA[(cloud3_y-1) * COLS + cloud3_x + 4] = (unsigned short)(' ' | (0x70 << 8));

    // ------ Надпись "WexHi" на траве ------
    cursor_row = ROWS - 3;
    cursor_col = (COLS - 5) / 2;
    text_color = 0x2F;
    prints("WexHi");

    // ------ Верхняя панель ------
    for (int c = 0; c < COLS; c++) {
        VGA[0 * COLS + c] = (unsigned short)(' ' | (0x70 << 8));
    }

    // ------ Иконки ------
    int icon_y = 1;
    int icon_spacing = 10;
    
    // Иконка Terminal
    int terminal_x = 5;
    for (int r = icon_y; r < icon_y + 3; r++) {
        for (int c = terminal_x; c < terminal_x + 3; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x00 << 8));
        }
    }
    cursor_row = icon_y + 1; cursor_col = terminal_x + 1;
    text_color = 0x0F;
    putchar('T');
    cursor_row = icon_y + 4; cursor_col = terminal_x - 1;
    text_color = 0x70;
    prints("Terminal");

    // Иконка Explorer
    int explorer_x = terminal_x + icon_spacing;
    for (int r = icon_y; r < icon_y + 3; r++) {
        for (int c = explorer_x; c < explorer_x + 3; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x00 << 8));
        }
    }
    cursor_row = icon_y + 1; cursor_col = explorer_x + 1;
    text_color = 0x0F;
    putchar('E');
    cursor_row = icon_y + 4; cursor_col = explorer_x - 2;
    text_color = 0x70;
    prints("Explorer");

    // Иконка Recycle Bin
    int recycle_x = explorer_x + icon_spacing;
    for (int r = icon_y; r < icon_y + 3; r++) {
        for (int c = recycle_x; c < recycle_x + 3; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (0x00 << 8));
        }
    }
    cursor_row = icon_y + 1; cursor_col = recycle_x + 1;
    text_color = 0x0F;
    putchar('R');
    cursor_row = icon_y + 4; cursor_col = recycle_x - 2;
    text_color = 0x70;
    prints("Recycle Bin");

    // ------ Основной цикл ------
    int cursor_x = COLS / 2;
    int cursor_y = ROWS / 2;
    int exit_desktop = 0;
    int last_second = -1; // Для обновления времени только при смене секунды
    
    // Переменные для хранения предыдущей позиции курсора
    int prev_cursor_x = cursor_x;
    int prev_cursor_y = cursor_y;

    // Начальное отображение времени
    char datetime[18];
    get_datetime(datetime);
    cursor_row = 0; cursor_col = (COLS - 17) / 2;
    text_color = 0x70;
    prints(datetime);
    prints("                   |||||| ENG");

    while (!exit_desktop) {
        // ------ Обновление времени только при смене секунды ------
        get_datetime(datetime);
        int current_second = (datetime[17] - '0') + (datetime[16] - '0') * 10;
        
        if (current_second != last_second) {
            cursor_row = 0; cursor_col = (COLS - 17) / 2;
            text_color = 0x70;
            prints(datetime);
            last_second = current_second;
        }

        // ------ Восстанавливаем предыдущую позицию курсора ------
        if (prev_cursor_x != cursor_x || prev_cursor_y != cursor_y) {
            // Восстанавливаем символ на предыдущей позиции курсора
            cursor_row = prev_cursor_y;
            cursor_col = prev_cursor_x;
            
            // Проверяем, находится ли позиция на иконке
            if (prev_cursor_y >= icon_y && prev_cursor_y <= icon_y + 2) {
                // Проверяем конкретные иконки и их центральные буквы
                if (prev_cursor_x >= terminal_x && prev_cursor_x <= terminal_x + 2) {
                    text_color = 0x00;
                    putchar(' ');
                    // Если это центральная позиция иконки Terminal, восстанавливаем букву 'T'
                    if (prev_cursor_y == icon_y + 1 && prev_cursor_x == terminal_x + 1) {
                        cursor_row = icon_y + 1;
                        cursor_col = terminal_x + 1;
                        text_color = 0x0F;
                        putchar('T');
                    }
                }
                else if (prev_cursor_x >= explorer_x && prev_cursor_x <= explorer_x + 2) {
                    text_color = 0x00;
                    putchar(' ');
                    // Если это центральная позиция иконки Explorer, восстанавливаем букву 'E'
                    if (prev_cursor_y == icon_y + 1 && prev_cursor_x == explorer_x + 1) {
                        cursor_row = icon_y + 1;
                        cursor_col = explorer_x + 1;
                        text_color = 0x0F;
                        putchar('E');
                    }
                }
                else if (prev_cursor_x >= recycle_x && prev_cursor_x <= recycle_x + 2) {
                    text_color = 0x00;
                    putchar(' ');
                    // Если это центральная позиция иконки Recycle Bin, восстанавливаем букву 'R'
                    if (prev_cursor_y == icon_y + 1 && prev_cursor_x == recycle_x + 1) {
                        cursor_row = icon_y + 1;
                        cursor_col = recycle_x + 1;
                        text_color = 0x0F;
                        putchar('R');
                    }
                }
                else {
                    // Не на иконке - восстанавливаем фон
                    restore_background(prev_cursor_x, prev_cursor_y);
                }
            }
            // Проверяем, находится ли позиция на тексте под иконками
            else if (prev_cursor_y == icon_y + 4) {
                if (prev_cursor_x >= terminal_x - 1 && prev_cursor_x <= terminal_x + 7) {
                    text_color = 0x70;
                    char* text = "Terminal";
                    int idx = prev_cursor_x - (terminal_x - 1);
                    if (idx >= 0 && idx < 8) {
                        putchar(text[idx]);
                    } else {
                        putchar(' ');
                    }
                }
                else if (prev_cursor_x >= explorer_x - 2 && prev_cursor_x <= explorer_x + 7) {
                    text_color = 0x70;
                    char* text = "Explorer";
                    int idx = prev_cursor_x - (explorer_x - 2);
                    if (idx >= 0 && idx < 9) {
                        putchar(text[idx]);
                    } else {
                        putchar(' ');
                    }
                }
                else if (prev_cursor_x >= recycle_x - 2 && prev_cursor_x <= recycle_x + 9) {
                    text_color = 0x70;
                    char* text = "Recycle Bin";
                    int idx = prev_cursor_x - (recycle_x - 2);
                    if (idx >= 0 && idx < 11) {
                        putchar(text[idx]);
                    } else {
                        putchar(' ');
                    }
                }
                else {
                    // Не на тексте - восстанавливаем фон
                    restore_background(prev_cursor_x, prev_cursor_y);
                }
            }
            // Верхняя панель
            else if (prev_cursor_y == 0) {
                text_color = 0x70;
                putchar(' ');
            }
            // Все остальные позиции - восстанавливаем фон
            else {
                restore_background(prev_cursor_x, prev_cursor_y);
            }
        }

        // ------ Сохраняем текущую позицию для следующей итерации ------
        prev_cursor_x = cursor_x;
        prev_cursor_y = cursor_y;

        // ------ Отрисовка курсора ------
        cursor_row = cursor_y;
        cursor_col = cursor_x;
        text_color = 0x4F;
        putchar('X');

        // ------ Неблокирующая обработка ввода ------
        if (inb(0x64) & 1) {
            unsigned char sc = inb(0x60);
            
            if (sc == 0xE0) {
                // Расширенный код
                while (!(inb(0x64) & 1));
                sc = inb(0x60);
                
                switch(sc) {
                    case 0x48: if (cursor_y > 1) cursor_y--; break;
                    case 0x50: if (cursor_y < ROWS-1) cursor_y++; break;
                    case 0x4B: if (cursor_x > 0) cursor_x--; break;
                    case 0x4D: if (cursor_x < COLS-1) cursor_x++; break;
                    case 0x1C: // Enter
                        if (cursor_y >= icon_y && cursor_y <= icon_y + 2) {
                            if (cursor_x >= terminal_x && cursor_x <= terminal_x + 2) {
                                exit_desktop = 1;
                                text_color = old_color;
                                clear_screen();
                                prints("Launching terminal...\n");
                            }
                            else if (cursor_x >= explorer_x && cursor_x <= explorer_x + 2) {
                                exit_desktop = 1;
                                text_color = old_color;
                                clear_screen();
                                prints("Launching Explorer...\n");
                                wexplorer_command();
                            }
                            else if (cursor_x >= recycle_x && cursor_x <= recycle_x + 2) {
                                exit_desktop = 1;
                                text_color = old_color;
                                clear_screen();
                                prints("Opening Recycle Bin...\n");
                            }
                        }
                        break;
                }
            }
            else if (sc == 0x1C) {
                // Enter
                if (cursor_y >= icon_y && cursor_y <= icon_y + 2) {
                    if (cursor_x >= terminal_x && cursor_x <= terminal_x + 2) {
                        exit_desktop = 1;
                        text_color = old_color;
                        clear_screen();
                        prints("Launching terminal...\n");
                    }
                    else if (cursor_x >= explorer_x && cursor_x <= explorer_x + 2) {
                        exit_desktop = 1;
                        text_color = old_color;
                        clear_screen();
                        prints("Launching Explorer...\n");
                        wexplorer_command();
                    }
                    else if (cursor_x >= recycle_x && cursor_x <= recycle_x + 2) {
                        exit_desktop = 1;
                        text_color = old_color;
                        clear_screen();
                        prints("Opening Recycle Bin...\n");
                    }
                }
            }
            else if (sc == 0x01) { // Escape
                exit_desktop = 1;
            }
        }

        // Короткая задержка вместо длинной блокирующей
        for (volatile int i = 0; i < 1000; i++);
    }

    text_color = old_color;
    clear_screen();
    prints("Exited desktop.\n> ");
}

// Вспомогательная функция для восстановления фона
void restore_background(int x, int y) {
    // Проверяем облака
    if ((y == 3 && x >= 10 && x <= 16) ||
        (y == 2 && x >= 12 && x <= 14) ||
        (y == 6 && x >= 40 && x <= 44) ||
        (y == 5 && x >= 41 && x <= 43) ||
        (y == 4 && x >= 60 && x <= 65) ||
        (y == 3 && x >= 61 && x <= 64)) {
        text_color = 0x70;
        putchar(' ');
        return;
    }
    
    // Стандартный фон
    if (y > ROWS - 5) {
        // Трава
        int grass_pattern = (y + x) % 4;
        char grass_char = (grass_pattern == 0) ? '`' : ' ';
        int grass_color = 0x20 + grass_pattern;
        text_color = grass_color;
        putchar(grass_char);
    } else {
        // Небо
        int sky_color = 0x10 + (y * 2 / ROWS);
        text_color = sky_color;
        putchar(' ');
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
    prints("OS Version: WexOS Shell v1.0 - Update UI\n");
}

void osinfo_command() {
    prints("=== System Information ===\n");
    
    // BIOS информация
    prints("BIOS Information:\n");
    outb(0x70, 0x0E);
    unsigned char bios_major = inb(0x71);
    outb(0x70, 0x0F);
    unsigned char bios_minor = inb(0x71);
    char buf[10];
    prints("  Version: ");
    itoa(bios_major, buf, 10);
    prints(buf);
    prints(".");
    itoa(bios_minor, buf, 10);
    prints(buf);
    newline();
    
    // CPU информация
    prints("\nCPU Information:\n");
    char vendor[13] = {0};
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
    prints("  Vendor: ");
    prints(vendor);
    newline();
    
    __asm__ volatile("mov $1, %%eax; cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    prints("  Family: ");
    itoa((eax >> 8) & 0x0F, buf, 10);
    prints(buf);
    prints("  Model: ");
    itoa((eax >> 4) & 0x0F, buf, 10);
    prints(buf);
    prints("  Stepping: ");
    itoa(eax & 0x0F, buf, 10);
    prints(buf);
    newline();
    
    // Память
    prints("\nMemory Information:\n");
    outb(0x70, 0x30);
    unsigned short base_mem_low = inb(0x71);
    outb(0x70, 0x31);
    unsigned short base_mem_high = inb(0x71);
    unsigned short base_memory = (base_mem_high << 8) | base_mem_low;
    
    outb(0x70, 0x34);
    unsigned short ext_mem_low = inb(0x71);
    outb(0x70, 0x35);
    unsigned short ext_mem_high = inb(0x71);
    unsigned short ext_memory = (ext_mem_high << 8) | ext_mem_low;
    
    unsigned int total_memory_kb = base_memory + ext_memory;
    unsigned int total_memory_mb = total_memory_kb / 1024;
    
    prints("  Total Memory: ");
    itoa(total_memory_mb, buf, 10);
    prints(buf);
    prints(" MB\n");
    
    // OS информация
    prints("\nOS Information:\n");
    prints("  Name: WexOS TinyShell\n");
    prints("  Version: 1.0\n");
    prints("  Build: 2025-09-05\n");
    
    prints("==========================\n");
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

void install_disk() {
    prints("\nWARNING: ALL DISKS INCLUDING BOOT DISKS WILL BE FORMATTED TO WexFS FOR OS INSTALLATION.\n");
    prints("CONTINUE? Y/N: ");

    char confirm = keyboard_getchar();
    putchar(confirm);
    newline();

    if (confirm != 'Y' && confirm != 'y') {
        prints("Installation cancelled.\n");
        return;
    }

    char password[64] = {0};

    // Установка пароля
    prints("Do you want to set a password for your user? Y/N: ");
    char pass_confirm = keyboard_getchar();
    putchar(pass_confirm);
    newline();

    if (pass_confirm == 'Y' || pass_confirm == 'y') {
        while (1) {
            prints("Enter the password: ");
            int len = 0;
            char c;
            while ((c = keyboard_getchar()) != '\n' && len < 63) {
                password[len++] = c;
            }
            password[len] = '\0';
            newline();

            prints("Enter the password again: ");
            char verify[64];
            len = 0;
            while ((c = keyboard_getchar()) != '\n' && len < 63) {
                verify[len++] = c;
            }
            verify[len] = '\0';
            newline();

            if (strcmp(password, verify) == 0) {
                prints("Password set successfully!\n");
                break;
            } else {
                prints("Passwords do not match. Try again.\n");
            }
        }
    }

    // Форматирование и создание директорий
    prints("Formatting disks to WexFS...\n");
    prints("Removing old system directories if they exist...\n");
    fs_format();

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
    fs_mkdir("mnt");
    fs_mkdir("mnt/rootdisk");
    fs_mkdir("mnt/Z:");
    fs_mkdir("tmp");
    fs_mkdir("tmp/null");
    fs_mkdir("dev");
    fs_mkdir("dev/usb");
    fs_mkdir("dev/usb 3.0");
    fs_mkdir("dev/usb 2.0");
    fs_mkdir("dev/usb 1.0");
    fs_mkdir("dev/CDROM");
    fs_mkdir("dev/floppy");

    // Копирование системных файлов
    prints("Copying system files...\n");
    fs_touch("SystemRoot/bin/taskmgr.bin");
    fs_touch("SystemRoot/bin/kernel.bin");
    fs_touch("SystemRoot/bin/calc.bin");
    fs_touch("SystemRoot/logs/config.cfg");
    fs_touch("SystemRoot/drivers/keyboard.sys");
    fs_touch("SystemRoot/drivers/mouse.sys");
    fs_touch("SystemRoot/drivers/vga.sys");
    fs_touch("SystemRoot/kerneldrivers/kernel.sys");
    fs_touch("SystemRoot/kerneldrivers/ntrsys.sys");

    // Копирование загрузочных файлов
    prints("Copying boot files...\n");
    fs_touch("boot/UEFI/grub.cfg");
    fs_touch("boot/Legacy/MBR.BIN");
    fs_touch("boot/Legacy/signature.cfg");

    // Копирование файлов системы
    prints("Copying filesystem files...\n");
    fs_touch("filesystem/WexFs/touch.bin");
    fs_touch("filesystem/WexFs/mkdir.bin");
    fs_touch("filesystem/WexFs/size.bin");
    fs_touch("filesystem/WexFs/cd.bin");
    fs_touch("filesystem/WexFs/ls.bin");
    fs_touch("filesystem/WexFs/copy.bin");
    fs_touch("filesystem/WexFs/rm.bin");

    // Конфигурация системы
    prints("Creating system configuration...\n");
    fs_touch("SystemRoot/config/autorun.cfg");
    FSNode* autorun_file = fs_find_file("SystemRoot/config/autorun.cfg");
    if (autorun_file) {
        strcpy(autorun_file->content, "desktop");
        autorun_file->size = strlen("desktop");
        prints("Desktop autorun configured\n");
    }

    // Запись пароля
    if (password[0] != '\0') {
        fs_touch("SystemRoot/config/pass.cfg");
        FSNode* passfile = fs_find_file("SystemRoot/config/pass.cfg");
        if (passfile) {
            strcpy(passfile->content, password);
            passfile->size = strlen(password);
            fs_mark_dirty();
            fs_save_to_disk();
        }
    }

    // Завершение установки и перезагрузка
    prints("\nInstallation completed successfully!\n");
    prints("Please reboot to start the installed system.\n");
    prints("Reboot now? Y/N: ");

    char reboot_confirm = keyboard_getchar();
    putchar(reboot_confirm);
    newline();

    if (reboot_confirm == 'Y' || reboot_confirm == 'y') {
        reboot_system();
    } else {

     }
}



void autorun_save_config(const char* command) {
    // Создаем или находим файл автозапуска
    FSNode* autorun_file = fs_find_file(AUTORUN_FILE);
    
    if (!autorun_file) {
        // Создаем директорию config если её нет
        fs_mkdir("SystemRoot/config");
        fs_touch(AUTORUN_FILE);
        autorun_file = fs_find_file(AUTORUN_FILE);
    }
    
    if (autorun_file) {
        strcpy(autorun_file->content, command);
        autorun_file->size = strlen(command);
        fs_mark_dirty();
        fs_save_to_disk();
    }
}

void autorun_execute(void) {
    // Сохраняем текущую директорию
    char old_dir[MAX_PATH];
    strcpy(old_dir, current_dir);
    
    // Ищем файл во всей файловой системе (без смены директории)
    int found = 0;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(fs_cache[i].name, "SystemRoot/config/autorun.cfg") == 0 && !fs_cache[i].is_dir) {
            FSNode* autorun_file = &fs_cache[i];
            
            if (autorun_file->size > 0) {
                // Копируем содержимое
                strcpy(autorun_command_buf, autorun_file->content);
                
                // Убираем символы переноса строки
                char* newline = strchr(autorun_command_buf, '\n');
                if (newline) *newline = '\0';
                char* cr = strchr(autorun_command_buf, '\r');
                if (cr) *cr = '\0';
                
                // Убираем пробелы
                trim_whitespace(autorun_command_buf);
                
                if (strlen(autorun_command_buf) > 0) {
                    prints("Executing autorun: '");
                    prints(autorun_command_buf);
                    prints("'\n");
                    run_command(autorun_command_buf);
                    found = 1;
                }
            }
            break;
        }
    }
    
    // Восстанавливаем директорию
    strcpy(current_dir, old_dir);
}

void autorun_command(const char* arg) {
    if (arg == NULL || strlen(arg) == 0) {
        // Показать текущую конфигурацию
        prints("AutoRun: ");
        if (autorun_enabled && autorun_command_buf[0] != '\0') {
            prints("'");
            prints(autorun_command_buf);
            prints("'");
        } else {
            prints("disabled");
        }
        prints("\n");
        return;
    }
    
    // Разбираем аргументы
    char command[128] = {0};
    char state[10] = {0};
    
    // Копируем аргументы
    char args[256];
    strcpy(args, arg);
    
    // Проверяем на list
    if (strcmp(args, "list") == 0) {
        prints("AutoRun status: ");
        prints(autorun_enabled ? "ENABLED" : "DISABLED");
        prints("\nCommand: ");
        if (autorun_command_buf[0] != '\0') {
            prints(autorun_command_buf);
        } else {
            prints("<not set>");
        }
        prints("\n");
        return;
    }
    
    // Ищем первый пробел для разделения команды и состояния
    char* space = strchr(args, ' ');
    if (space) {
        *space = '\0';
        strcpy(command, args);
        
        // Пропускаем пробелы после команды
        char* state_ptr = space + 1;
        while (*state_ptr == ' ') state_ptr++;
        
        // Копируем состояние (on/off)
        char* state_end = strchr(state_ptr, ' ');
        if (state_end) {
            *state_end = '\0';
        }
        strcpy(state, state_ptr);
    } else {
        // Если только один аргумент
        strcpy(command, args);
    }
    
    // Проверяем состояние (on/off)
    if (strlen(state) == 0) {
        prints("Usage: autorun <command> on/off\n");
        return;
    }
    
    if (strcmp(state, "on") != 0 && strcmp(state, "off") != 0) {
        prints("Error: State must be 'on' or 'off'\n");
        return;
    }
    
    if (strcmp(state, "on") == 0) {
        // Включаем автозапуск для команды
        if (strlen(command) >= AUTORUN_MAX_COMMAND) {
            prints("Error: Command too long\n");
            return;
        }
        
        if (strlen(command) == 0) {
            prints("Error: Command cannot be empty\n");
            return;
        }
        
        strcpy(autorun_command_buf, command);
        autorun_enabled = 1;
        autorun_save_config(autorun_command_buf);
        
        prints("AutoRun enabled: '");
        prints(command);
        prints("'\n");
    }
    else if (strcmp(state, "off") == 0) {
        // Выключаем автозапуск
        autorun_enabled = 0;
        autorun_save_config(autorun_command_buf);
        prints("AutoRun disabled\n");
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

/* Exit command - clear screen and show login */
void exit_command(void) {
    clear_screen();
    check_login();
}

/* Configuration screen */
#define CONFIG_TITLE "{ SYSTEM CONFIGURATIONS }"
#define HEADER_COLOR 0x1F
#define FIELD_COLOR 0x07
#define SELECTED_COLOR 0x4F
#define VALUE_COLOR 0x0F
#define FOOTER_COLOR 0x70

typedef struct {
    const char* label;
    int x_label;
    int x_value;
    int max_width;
    int min_val;
    int max_val;
} FieldConfig;

const FieldConfig fields[] = {
    {"User-Name:", 5, 20, 30, 0, 0},
    {"User-Pass:", 5, 20, 30, 0, 0},
    {"Admin-Pass:", 5, 20, 30, 0, 0},
    {"Root-Pass:", 5, 20, 30, 0, 0},
    {"Screen-Timeout:", 5, 22, 6, 60, 3600},
    {"Max-Users:", 5, 20, 3, 1, 100},
    {"Auto-Login:", 5, 20, 10, 0, 0},
    {"Debug-Mode:", 5, 20, 10, 0, 0},
    {"Log-Level:", 5, 20, 1, 0, 3}
};

#define FIELD_COUNT (sizeof(fields) / sizeof(fields[0]))

void draw_box(int x1, int y1, int x2, int y2, int color) {
    unsigned char old_color = text_color;
    text_color = color;
    
    // Углы
    cursor_row = y1; cursor_col = x1; putchar(0xC9); // ┌
    cursor_row = y1; cursor_col = x2; putchar(0xBB); // ┐
    cursor_row = y2; cursor_col = x1; putchar(0xC8); // └
    cursor_row = y2; cursor_col = x2; putchar(0xBC); // ┘
    
    // Горизонтальные линии
    for (int x = x1 + 1; x < x2; x++) {
        cursor_row = y1; cursor_col = x; putchar(0xCD); // ─
        cursor_row = y2; cursor_col = x; putchar(0xCD); // ─
    }
    
    // Вертикальные линии
    for (int y = y1 + 1; y < y2; y++) {
        cursor_row = y; cursor_col = x1; putchar(0xBA); // │
        cursor_row = y; cursor_col = x2; putchar(0xBA); // │
    }
    
    text_color = old_color;
}

void clear_area(int x1, int y1, int x2, int y2, int color) {
    unsigned char old_color = text_color;
    text_color = color;
    
    for (int y = y1; y <= y2; y++) {
        cursor_row = y;
        for (int x = x1; x <= x2; x++) {
            cursor_col = x;
            putchar(' ');
        }
    }
    
    text_color = old_color;
}

void get_password_input(char* buffer, int max_len, int row, int col, int width) {
    char temp_buffer[32];
    int temp_len = 0;
    
    for (int i = 0; i < 32; i++) temp_buffer[i] = 0;
    
    while(1) {
        cursor_row = row;
        cursor_col = col;
        for(int i = 0; i < width; i++) putchar(' ');
        
        cursor_row = row;
        cursor_col = col;
        for(int i = 0; i < temp_len; i++) putchar('*');
        
        cursor_row = row;
        cursor_col = col + temp_len;
        
        char c = keyboard_getchar();
        if (c == '\n') break;
        else if (c == '\b') {
            if (temp_len > 0) {
                temp_len--;
                temp_buffer[temp_len] = '\0';
            }
        } else if (c >= 32 && c <= 126 && temp_len < max_len - 1) {
            temp_buffer[temp_len] = c;
            temp_len++;
            temp_buffer[temp_len] = '\0';
        }
    }
    
    strcpy(buffer, temp_buffer);
}

void draw_config_screen(int current_field) {
    clear_screen();
    
    cursor_row = 1;
    cursor_col = (COLS - strlen(CONFIG_TITLE)) / 2;
    text_color = HEADER_COLOR;
    prints(CONFIG_TITLE);
    
    draw_box(3, 3, COLS-3, 17, FIELD_COLOR);
    
    for (int i = 0; i < FIELD_COUNT; i++) {
        cursor_row = i + 5;
        cursor_col = fields[i].x_label;
        
        if (i == current_field) {
            text_color = SELECTED_COLOR;
            clear_area(fields[i].x_label, i + 5, fields[i].x_label + fields[i].max_width + 15, i + 5, SELECTED_COLOR);
            cursor_row = i + 5;
            cursor_col = fields[i].x_label;
        } else {
            text_color = FIELD_COLOR;
        }
        
        prints(fields[i].label);
        
        cursor_col = fields[i].x_value;
        text_color = (i == current_field) ? SELECTED_COLOR : VALUE_COLOR;
        
        switch(i) {
            case 0: prints(temp_config.user_name); break;
            case 1: 
            case 2: 
            case 3: prints("********"); break;
            case 4: {
                char timeout_str[10];
                itoa(temp_config.screen_timeout, timeout_str, 10);
                prints(timeout_str); prints(" sec");
                break;
            }
            case 5: {
                char users_str[10];
                itoa(temp_config.max_users, users_str, 10);
                prints(users_str);
                break;
            }
            case 6: prints(temp_config.auto_login ? "[X] Enabled" : "[ ] Disabled"); break;
            case 7: prints(temp_config.debug_mode ? "[X] Enabled" : "[ ] Disabled"); break;
            case 8: {
                char level_str[2] = { '0' + temp_config.log_level, 0 };
                prints(level_str);
                prints(" (");
                switch(temp_config.log_level) {
                    case 0: prints("None"); break;
                    case 1: prints("Error"); break;
                    case 2: prints("Warning"); break;
                    case 3: prints("Debug"); break;
                }
                prints(")");
                break;
            }
        }
    }
    
    text_color = FOOTER_COLOR;
    cursor_row = 19;
    cursor_col = 0;
    clear_area(0, 19, COLS-1, 19, FOOTER_COLOR);
    
    cursor_row = 19;
    cursor_col = 2;
    prints("UP/DOWN: Navigate  ENTER: Edit  SPACE: Toggle  F9: Save  ESC: Cancel");
    
    cursor_row = 21;
    cursor_col = 2;
    text_color = FIELD_COLOR;
    clear_area(2, 21, COLS-3, 21, FIELD_COLOR);
    
    switch(current_field) {
        case 0: prints("Enter user name (1-30 characters)"); break;
        case 1: prints("Enter user password"); break;
        case 2: prints("Enter administration password"); break;
        case 3: prints("Enter root password"); break;
        case 4: prints("Screen timeout in seconds (60-3600)"); break;
        case 5: prints("Maximum number of users (1-100)"); break;
        case 6: prints("Toggle auto-login on startup"); break;
        case 7: prints("Toggle debug mode"); break;
        case 8: prints("Set log level: 0=None, 1=Error, 2=Warning, 3=Debug"); break;
    }
}

void edit_config_field(int field_num) {
    cursor_row = field_num + 5;
    cursor_col = fields[field_num].x_value;
    text_color = SELECTED_COLOR;
    
    switch(field_num) {
        case 0: 
            get_input(temp_config.user_name, 31, field_num + 5, 
                     fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 1: 
            get_password_input(temp_config.user_pass, 31, field_num + 5, 
                              fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 2: 
            get_password_input(temp_config.admin_pass, 31, field_num + 5, 
                              fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 3: 
            get_password_input(temp_config.root_pass, 31, field_num + 5, 
                              fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 4: 
            get_number_input(&temp_config.screen_timeout, fields[field_num].min_val, 
                           fields[field_num].max_val, field_num + 5, 
                           fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 5: 
            get_number_input(&temp_config.max_users, fields[field_num].min_val, 
                           fields[field_num].max_val, field_num + 5, 
                           fields[field_num].x_value, fields[field_num].max_width);
            break;
        case 6: 
            temp_config.auto_login = !temp_config.auto_login;
            break;
        case 7: 
            temp_config.debug_mode = !temp_config.debug_mode;
            break;
        case 8: 
            get_number_input(&temp_config.log_level, fields[field_num].min_val, 
                           fields[field_num].max_val, field_num + 5, 
                           fields[field_num].x_value, fields[field_num].max_width);
            break;
    }
    
    text_color = FIELD_COLOR;
}

void show_message(const char* message, int color) {
    unsigned char old_color = text_color;
    text_color = color;
    
    clear_area(0, 17, COLS-1, 17, color);
    cursor_row = 17;
    cursor_col = (COLS - strlen(message)) / 2;
    prints(message);
    
    text_color = old_color;
}

void config_command() {
    temp_config = sys_config;
    
    int current_field = 0;
    int exit_config = 0;
    int save_config = 0;
    
    while(!exit_config) {
        draw_config_screen(current_field);
        
        // Обработка клавиш как в WexExplorer
        int action = 0;
        while (action == 0) {
            unsigned char st = inb(0x64);
            if (st & 1) {
                unsigned char sc = inb(0x60);
                
                if (sc == 0xE0) {
                    while (!(inb(0x64) & 1));
                    sc = inb(0x60);
                    
                    if (sc == 0x48) { // Up
                        current_field = (current_field > 0) ? current_field - 1 : FIELD_COUNT - 1;
                        action = 1;
                    }
                    else if (sc == 0x50) { // Down
                        current_field = (current_field < FIELD_COUNT - 1) ? current_field + 1 : 0;
                        action = 1;
                    }
                    else if (sc == 0x1C) { // Enter
                        action = 2;
                    }
                } 
                else if (sc == 0x1C) { // Enter
                    action = 2;
                }
                else if (sc == 0x01) { // ESC
                    action = 3;
                }
                else if (sc == 0x39) { // Space
                    action = 4;
                }
                else if (sc == 0x43) { // F9
                    action = 5;
                }
            }
            
            for (volatile int i = 0; i < 5000; i++);
        }
        
        switch(action) {
            case 1: break; // Навигация
            case 2: edit_config_field(current_field); break;
            case 4: 
                if (current_field == 6 || current_field == 7) {
                    edit_config_field(current_field);
                }
                break;
            case 5: 
                save_config = 1;
                exit_config = 1;
                break;
            case 3: 
                exit_config = 1;
                break;
        }
    }
    
    clear_screen();
    
    if (save_config) {
        sys_config = temp_config;
        show_message("Configuration successfully saved!", 0x2F);
    } else {
        show_message("Changes discarded!", 0x4F);
    }
    
    cursor_row = 19;
    cursor_col = (COLS - 28) / 2;
    text_color = FIELD_COLOR;
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
    
    const char* commands[] = {
        "help",     "echo",     "reboot",   "shutdown", "clear",
        "ls",       "cd",       "mkdir",    "rm",       "touch",
        "copy",     "writer",   "ps",       "kill",     "coreview",
        "color",    "colorf",   "install",  "config",   "memory",
        "cpu",      "date",     "watch",    "biosver",  "calc",
        "time",     "size",     "osver",    "history",  "format",
        "fsck",     "cat",      "explorer", "osinfo",   "autorun",
		"exit", NULL
		
    };
    
    prints("Available commands:\n");
    prints("==================\n\n");
    
    int total_commands = 0;
    while (commands[total_commands] != NULL) {
        total_commands++;
    }
    
    int commands_per_column = (total_commands + 1) / 2;
    int col_width = 15;
    
    for (int i = 0; i < commands_per_column; i++) {
        // Левый столбец
        cursor_col = 2;
        prints(commands[i]);
        
        // Выравнивание левого столбца
        int spaces = col_width - strlen(commands[i]);
        for (int s = 0; s < spaces; s++) {
            putchar(' ');
        }
        
        // Правый столбец (если есть)
        if (i + commands_per_column < total_commands) {
            prints(commands[i + commands_per_column]);
        }
        
        newline();
    }
    
    newline();
    prints("These are all commands.\n");
    
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
	else if(strcasecmp(line, "install") == 0) install_disk();
	else if(strcasecmp(line, "osinfo") == 0) osinfo_command();
	else if(strcasecmp(line, "desktop") == 0) cmd_desktop();
    else if(strcasecmp(line, "ls") == 0) fs_ls();
	else if(strcasecmp(line, "format") == 0) fs_format();
	else if(strcasecmp(line, "fsck") == 0) fsck_command();
    else if(strcasecmp(line, "cd") == 0) { while(*p == ' ') p++; if(*p) fs_cd(p); else prints("Usage: cd <directory>\n"); }
    else if(strcasecmp(line, "mkdir") == 0) { while(*p == ' ') p++; if(*p) fs_mkdir(p); else prints("Usage: mkdir <name>\n"); }
    else if(strcasecmp(line, "touch") == 0) { while(*p == ' ') p++; if(*p) fs_touch(p); else prints("Usage: touch <name>\n"); }
    else if(strcasecmp(line, "rm") == 0) { while(*p == ' ') p++; if(*p) fs_rm(p); else prints("Usage: rm <name>\n"); }
	else if(strcasecmp(line, "explorer") == 0) wexplorer_command();
	else if(strcasecmp(line, "exit") == 0) exit_command();
	else if(strcasecmp(line, "autorun") == 0) { 
    while(*p == ' ') p++; 
    if(*p) autorun_command(p); 
    else prints("Usage: autorun <command> on/off\n       autorun list\n"); 
}
	else if(strcasecmp(line, "cat") == 0) { 
    while(*p == ' ') p++; 
	if(*p) fs_cat(p); 
    else prints("Usage: cat <filename>\n"); 
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
    else if(strcasecmp(line, "writer") == 0) { while(*p == ' ') p++; if(*p) writer_command(p); else prints("Usage: writer <filename>\n"); }
    else if(strcasecmp(line, "ps") == 0) ps_command();
    else if(strcasecmp(line, "kill") == 0) { while(*p == ' ') p++; if(*p) kill_command(p); else prints("Usage: kill <name or pid>\n"); }
    else if(strcasecmp(line, "coreview") == 0) coreview_command();
    else if(strcasecmp(line, "color") == 0) { while(*p == ' ') p++; if(*p) text_color = (*p - '0'); else prints("Usage: color <0-7>\n"); }
        else if(strcasecmp(line, "colorf") == 0) { while(*p == ' ') p++; if(*p) text_color = (*p - '0') | (text_color & 0xF0); else prints("Usage: colorf <0-7>\n"); }
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
// Предполагается, что inb уже определена как static inline

unsigned char get_key() {
    while (!(inb(0x64) & 0x01)); // Wait for data
    return inb(0x60); // Read scan code
}

void memory_command() {
    unsigned int start_addr = 0x1000;
    unsigned int end_addr = 0x2000;
    unsigned int current_addr = start_addr;
    int lines_per_page = 20;
    int first_run = 1;
    int needs_redraw = 1;

    while (1) {
        if (needs_redraw) {
            if (first_run) {
                prints("\033[2J\033[H"); // Очистка и заголовок только при первом запуске
                prints("Memory Dump (PgUp/PgDn to scroll, Esc to exit):\n");
                first_run = 0;
            } else {
                prints("\033[H"); // Перемещение курсора в начало без очистки
            }

            for (int i = 0; i < lines_per_page && current_addr < end_addr; i++, current_addr += 16) {
                char addr_str[10];
                itoa(current_addr, addr_str, 16);
                prints("0x");
                for (int j = strlen(addr_str); j < 4; j++) prints("0");
                prints(addr_str);
                prints(": ");

                for (int j = 0; j < 16; j++) {
                    unsigned char val = *(unsigned char*)(current_addr + j);
                    if ((unsigned int)(current_addr + j) < end_addr) {
                        char val_str[3];
                        itoa(val, val_str, 16);
                        if (val < 0x10) prints("0");
                        prints(val_str);
                        prints(" ");
                    }
                }
                prints(" |");

                for (int j = 0; j < 16; j++) {
                    unsigned char val = *(unsigned char*)(current_addr + j);
                    if ((unsigned int)(current_addr + j) < end_addr) {
                        if (val >= 32 && val <= 126) putchar(val);
                        else putchar('.');
                    }
                }
                prints("|\n");
            }
            needs_redraw = 0; // Сброс флага после отрисовки
        }

        unsigned char key = get_key();
        if (key == 0x01) break; // Esc
        if (key == 0x49 && current_addr > start_addr) { // PgUp
            current_addr -= lines_per_page * 16;
            if (current_addr < start_addr) current_addr = start_addr;
            needs_redraw = 1; // Требуется перерисовка
        }
        if (key == 0x51 && current_addr + lines_per_page * 16 <= end_addr) { // PgDn
            current_addr += lines_per_page * 16;
            needs_redraw = 1; // Требуется перерисовка
        }
    }
}

void show_loading_screen(void) {
    unsigned char old_color = text_color;
    
    // Очищаем экран черным цветом один раз
    for(int r = 0; r < ROWS; r++) {
        for(int c = 0; c < COLS; c++) {
            VGA[r * COLS + c] = (unsigned short)(' ' | (LOADING_BG_COLOR << 8));
        }
    }
    
    // Рисуем статичный логотип один раз
    draw_wexos_logo();
    
    // Анимация загрузки - 2.5 секунды
    int total_frames = 340; 
    int frame_delay = 83000; // микросекунды на кадр
    
    for(int frame = 0; frame < total_frames; frame++) {
        int progress = (frame * 100) / total_frames;
        
        // Только обновляем анимацию, не перерисовываем весь экран
        draw_loading_animation(frame, progress);
        
        // Задержка для плавной анимации
        for(volatile int i = 0; i < frame_delay; i++);
    }
    
    // Финальный прогресс 100%
    draw_loading_animation(total_frames, 100);
    
    // Короткая пауза перед переходом
    for(volatile int i = 0; i < 1500000; i++);
    
    text_color = old_color;
}

void draw_wexos_logo(void) {
    text_color = LOADING_TEXT_COLOR;
    
    // Красивый ASCII арт "WexOS"
    cursor_row = 5;
    cursor_col = 25;
    prints("__      __        ____    ");
    
    cursor_row = 6;
    cursor_col = 25;
    prints("| | /| / /____ __/ __ \\___");
    
    cursor_row = 7;
    cursor_col = 25;
    prints("| |/ |/ / -_) \\ / /_/ (_-<");
    
    cursor_row = 8;
    cursor_col = 25;
    prints("|__/|__/\\__/_\\_\\\\____/___/");
    
    // Подпись версии
    cursor_row = 10;
    cursor_col = 35;
    prints("WexOS v1.0");
}

void draw_loading_animation(int frame, int progress) {
    // Очищаем только область анимации (строки 12-16)
    for(int r = 12; r <= 16; r++) {
        cursor_row = r;
        cursor_col = 30;
        for(int c = 0; c < 40; c++) {
            putchar(' ');
        }
    }
    
    // Текст "Loading" с анимацией
    cursor_row = 12;
    cursor_col = 35;
    text_color = LOADING_ACCENT_COLOR;
    prints("Loading");
    
    // Анимированные точки после "Loading"
    int dots = (frame / 8) % 4;
    for(int i = 0; i < 3; i++) {
        if(i < dots) {
            putchar('.');
        } else {
            putchar(' ');
        }
    }
    
    // Процент загрузки
    cursor_row = 14;
    cursor_col = 38;
    text_color = LOADING_TEXT_COLOR;
    
    char progress_str[5];
    itoa(progress, progress_str, 10);
    
    // Красивое отображение процента
    if(progress < 10) {
        prints("  ");
        prints(progress_str);
    } else if(progress < 100) {
        prints(" ");
        prints(progress_str);
    } else {
        prints(progress_str);
    }
    prints("%");
    
    // Простая анимация прогресса (точки)
    cursor_row = 16;
    cursor_col = 32;
    text_color = LOADING_ACCENT_COLOR;
    
    int total_dots = 20;
    int filled_dots = (progress * total_dots) / 100;
    
    for(int i = 0; i < total_dots; i++) {
        if(i < filled_dots) {
            putchar(0xFE); // Заполненный блок
        } else {
            putchar(0xB0); // Светлый затененный блок
        }
    }
    
    // Информационная строка
    cursor_row = 18;
    cursor_col = 30;
    text_color = LOADING_TEXT_COLOR;
    prints("Initializing system components");
}
/* Kernel main */
void _start() {
    text_color = 0x07;

    // --- Показываем экран загрузки ---
    show_loading_screen();
    
    // Очищаем экран после загрузки
    clear_screen();

    // Инициализация процессов
    init_processes();
    
    // Инициализация файловой системы
    fs_init();

    // --- Проверка пароля при загрузке ---
    if (!check_login()) {
        prints("Login failed. System halted.\n");
        while(1) { __asm__ volatile("hlt"); }
    }

    // --- ЗАПУСК АВТОЗАПУСКА ПОСЛЕ ВХОДА ---
    autorun_execute();

    prints("WexOS Shell v1.0\n");
    prints("Type 'help' for commands\n\n");

    char cmd_buf[128];
    int cmd_idx = 0;
    int history_pos = -1;

    while (1) {
        prints("SystemRoot> ");

        while (1) {
            cursor_row = cursor_row;
            cursor_col = cursor_col;

            char c = getch_with_arrows();

            if (c == '\n') {
                cmd_buf[cmd_idx] = '\0';
                newline();
                
                // Выполняем команду только если она не пустая
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

                    // Очищаем строку и перерисовываем
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("SystemRoot> ");
                    prints(cmd_buf);
                }
            } else if (c == 'D') { // стрелка вниз
                if (history_pos > 0) {
                    history_pos--;
                    strcpy(cmd_buf, command_history[history_count - 1 - history_pos]);
                    cmd_idx = strlen(cmd_buf);

                    // Очищаем строку и перерисовываем
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("SystemRoot> ");
                    prints(cmd_buf);
                } else if (history_pos == 0) {
                    history_pos = -1;
                    cmd_idx = 0;
                    cmd_buf[0] = '\0';

                    // Очищаем строку и перерисовываем
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    for (int i = 0; i < COLS; i++) {
                        putchar(' ');
                    }
                    
                    cursor_row = cursor_row;
                    cursor_col = 0;
                    prints("SystemRoot> ");
                }
            } else if (c >= 32 && c <= 126 && cmd_idx < 127) {
                cmd_buf[cmd_idx] = c;
                cmd_idx++;
                cmd_buf[cmd_idx] = '\0';
                putchar(c);
            } else if (c == 0x01) { // ESC
                cmd_idx = 0;
                cmd_buf[0] = '\0';
                
                // Очищаем строку и перерисовываем
                cursor_row = cursor_row;
                cursor_col = 0;
                for (int i = 0; i < COLS; i++) {
                    putchar(' ');
                }
                
                cursor_row = cursor_row;
                cursor_col = 0;
                prints("SystemRoot> ");
            } else if (c == '\t') { // TAB
                // Просто добавляем пробел при нажатии TAB
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
