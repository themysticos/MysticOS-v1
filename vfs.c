// vfs.c — слой виртуальной ФС MysticOS
// Объединяет FAT12/FAT16 и MSFS (Mystic Soft File System) под
// единым API с поддержкой путей и текущего каталога (cwd).

#define SECTOR_SIZE 512
#include "fs.h"
#include "fat.h"

// --- MSFS (msfs.c) ---
int  msfs_probe(void);
void msfs_format(void);
int  msfs_lookup(int base, const char* path);
int  msfs_is_dir(int inode);
void msfs_list(int base, const char* path);
void msfs_read(int base, const char* path);
void msfs_write(int base, const char* path, const char* content);
int  msfs_mkdir(int base, const char* path);
int  msfs_remove(int base, const char* path);
int  msfs_read_buf(int base, const char* path, unsigned char* buf, unsigned int max);



// --- FAT (fat.c) — путь-ориентированный API ---
int  fat_find(int base_dir, const char* path);
void fat_list(int base_dir, const char* path);
void fat_read(int base_dir, const char* path);
void fat_write_path(int base_dir, const char* path, const char* content);
int  fat_mkdir(int base_dir, const char* path);
int  fat_cd(const char* path, unsigned int* out_cluster);
int  fat_read_buf(int base_dir, const char* path, unsigned char* buf, unsigned int max);

void print_string(const char* str);
void put_char(char c);

// ============================================================
// Состояние VFS
// ============================================================
// ВНИМАНИЕ: не static и с явным значением — уводим из «голого» BSS,
// который не зануляется загрузчиком (см. entry.asm).
volatile fs_type_t current_fs = FS_UNKNOWN;
static char g_cwd[FS_PATH_MAX] = "/";
static unsigned int g_fat_cwd_cluster = 0;  // 0 = корень FAT

fs_type_t vfs_get_type(void) { return current_fs; }
const char* vfs_getcwd(void) { return g_cwd; }

static void str_copy(char* d, const char* s) {
    int i = 0;
    while (s[i] && i < FS_PATH_MAX - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

// ============================================================
// Определение ФС
// ============================================================
fs_type_t vfs_detect(void) {
    unsigned char sector[SECTOR_SIZE];
    extern void ata_read_sector(unsigned int lba, unsigned char* buffer);
    ata_read_sector(0, sector);

    // MSFS: магия "MSFS" в первых 4 байтах
    if (sector[0] == 'M' && sector[1] == 'S' &&
        sector[2] == 'F' && sector[3] == 'S') {
        return FS_MSFS;
    }

    // FAT: bytes_per_sector == 512 (little-endian) по смещению 11
    unsigned short bps = sector[11] | (sector[12] << 8);
    if (bps == 512) return FS_FAT16;

    return FS_UNKNOWN;
}

void vfs_init(void) {
    current_fs = vfs_detect();
    if (current_fs == FS_MSFS && !msfs_probe()) current_fs = FS_UNKNOWN;
    g_cwd[0] = '/'; g_cwd[1] = 0;
    g_fat_cwd_cluster = 0;
}

const char* vfs_type_name(void) {
    switch (current_fs) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
        case FS_MSFS:  return "MSFS";
        default:       return "Unknown";
    }
}

static int msfs_cwd_inode(void) {
    int inode = msfs_lookup(0, g_cwd);
    return inode < 0 ? 0 : inode;
}

// ============================================================
// ls / cat / write / mkdir / rm
// ============================================================
void vfs_ls(const char* path) {
    if (current_fs == FS_MSFS) msfs_list(msfs_cwd_inode(), path);
    else if (current_fs == FS_FAT16 || current_fs == FS_FAT12)
        fat_list((int)g_fat_cwd_cluster, path);
    else print_string("Unknown filesystem.\n");
}

void vfs_cat(const char* path) {
    if (current_fs == FS_MSFS) msfs_read(msfs_cwd_inode(), path);
    else if (current_fs == FS_FAT16 || current_fs == FS_FAT12)
        fat_read((int)g_fat_cwd_cluster, path);
    else print_string("Unknown filesystem.\n");
}

void vfs_write(const char* path, const char* content) {
    if (current_fs == FS_MSFS) msfs_write(msfs_cwd_inode(), path, content);
    else if (current_fs == FS_FAT16 || current_fs == FS_FAT12)
        fat_write_path((int)g_fat_cwd_cluster, path, content);
    else print_string("Unknown filesystem.\n");
}

// Форматирует диск как MSFS и переключается на неё.
void vfs_format(void) {
    msfs_format();
    current_fs = FS_MSFS;
    g_cwd[0] = '/'; g_cwd[1] = 0;
    g_fat_cwd_cluster = 0;
    print_string("Formatted as MSFS.\n");
}

int vfs_mkdir(const char* path) {
    if (current_fs == FS_MSFS) return msfs_mkdir(msfs_cwd_inode(), path);
    if (current_fs == FS_FAT16 || current_fs == FS_FAT12)
        return fat_mkdir((int)g_fat_cwd_cluster, path);
    print_string("Unknown filesystem.\n");
    return 0;
}

int vfs_rm(const char* path) {
    if (current_fs == FS_MSFS) return msfs_remove(msfs_cwd_inode(), path);
    print_string("rm: not supported on this FS yet.\n");
    return 0;
}

// Читает файл целиком в буфер. Возвращает размер (>=0) или -1.
int vfs_read_file(const char* path, unsigned char* buf, unsigned int max) {
    if (current_fs == FS_MSFS)
        return msfs_read_buf(msfs_cwd_inode(), path, buf, max);
    if (current_fs == FS_FAT16 || current_fs == FS_FAT12)
        return fat_read_buf((int)g_fat_cwd_cluster, path, buf, max);
    print_string("read_file: unknown filesystem\n");
    return -1;
}

// ============================================================
// cd — с нормализацией пути
// ============================================================
// Разворачивает '.', '..' и '//' относительно g_cwd.
static void path_normalize(const char* path, char* out) {
    char tmp[FS_PATH_MAX];
    int t = 0;

    if (path[0] == '/') {
        tmp[t++] = '/';
    } else {
        int c = 0;
        while (g_cwd[c] && t < FS_PATH_MAX - 1) tmp[t++] = g_cwd[c++];
    }
    if (t == 0) tmp[t++] = '/';
    tmp[t] = 0;

    int p = 0;
    while (path[p]) {
        if (path[p] == '/') { p++; continue; }
        char comp[FS_PATH_MAX];
        int c = 0;
        while (path[p] && path[p] != '/' && c < FS_PATH_MAX - 1) comp[c++] = path[p++];
        comp[c] = 0;

        if (comp[0] == '.' && comp[1] == 0) continue;
        if (comp[0] == '.' && comp[1] == '.' && comp[2] == 0) {
            int tt = 0;
            while (tmp[tt]) tt++;
            if (tt > 1) {
                tt--;
                while (tt > 1 && tmp[tt - 1] != '/') tt--;
                tmp[tt] = 0;
            }
            continue;
        }

        int tt = 0;
        while (tmp[tt]) tt++;
        if (tt > 0 && tmp[tt - 1] != '/') tmp[tt++] = '/';
        int k = 0;
        while (comp[k] && tt < FS_PATH_MAX - 1) tmp[tt++] = comp[k++];
        tmp[tt] = 0;
    }

    int tt = 0;
    while (tmp[tt] && tt < FS_PATH_MAX - 1) { out[tt] = tmp[tt]; tt++; }
    if (tt == 0) { out[0] = '/'; tt = 1; }
    out[tt] = 0;
    // Схлопываем ведущие двойные слэши: //foo -> /foo, // -> /
    if (out[0] == '/' && out[1] == '/') {
        int src = 1, dst = 1;
        while (out[src] == '/') src++;
        while (out[src]) out[dst++] = out[src++];
        out[dst] = 0;
    }
    // Убираем завершающий слэш (кроме корня)
    tt = 0; while (out[tt]) tt++;
    if (tt > 1 && out[tt - 1] == '/') out[tt - 1] = 0;
}

int vfs_cd(const char* path) {
    char norm[FS_PATH_MAX];
    path_normalize(path, norm);

    if (current_fs == FS_MSFS) {
        int inode = msfs_lookup(0, norm);
        if (inode < 0 || !msfs_is_dir(inode)) {
            print_string("cd: no such directory\n");
            return 0;
        }
        str_copy(g_cwd, norm);
        return 1;
    }
    if (current_fs == FS_FAT16 || current_fs == FS_FAT12) {
        unsigned int cluster = 0;
        if (fat_cd(norm, &cluster)) {
            g_fat_cwd_cluster = cluster;
            str_copy(g_cwd, norm);
            return 1;
        }
        print_string("cd: no such directory\n");
        return 0;
    }
    print_string("Unknown filesystem.\n");
    return 0;
}
