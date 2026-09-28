// msfs.c — MSFS (Mystic Soft File System)
// Кастомная файловая система MysticOS. Версия 1.
//
// Идея (простая, но настоящая ФС):
//   * Суперблок в секторе 0 области MSFS:
//       - магия "MSFS", версия, размер сектора,
//       - число inode-записей, начало таблицы inode, начало данных,
//       - размер кластера (в секторах), общее число кластеров.
//   * Таблица inode — массив фиксированного размера (MSFS_MAX_INODES).
//     Каждая запись 64 байта:
//       name[32], type(0=free,1=file,2=dir), size,
//       first_cluster, parent, reserved[].
//   * Данные: кластеры, цепочка через таблицу указателей next[]
//     (хранится сразу после inode-таблицы). Каталоги — это
//     inode'ы с type=2; их содержимое (имена детей) мы находим,
//     сканируя inode-таблицу по полю parent.
//
// Это даёт настоящую вложенность каталогов, файлы произвольного
// размера (цепочки кластеров) и не требует рекурсивной структуры.

#define SECTOR_SIZE 512
#include "fs.h"

// --- Внешние символы ядра ---
extern void ata_read_sector(unsigned int lba, unsigned char* buffer);
extern void ata_write_sector(unsigned int lba, const unsigned char* buffer);
void print_string(const char* str);
void put_char(char c);
void print_int(int num);

// ============================================================
// Константы и структуры MSFS
// ============================================================

#define MSFS_MAGIC       "MSFS"
#define MSFS_VERSION     1
#define MSFS_MAX_INODES  64          // фиксированное число inode-записей
#define MSFS_INODE_SIZE  64          // байт на запись
#define MSFS_NAME_LEN    32          // макс. длина имени (вкл. '\0')
#define MSFS_MAX_CHAIN   16          // макс. кластеров на файл
#define MSFS_ROOT_INODE  0           // корневой каталог всегда inode 0
#define MSFS_TYPE_FREE   0
#define MSFS_TYPE_FILE   1
#define MSFS_TYPE_DIR    2

// Суперблок (помещается в первые 512 байт области)
typedef struct {
    char     magic[4];        // "MSFS"
    unsigned int version;
    unsigned int sector_size; // 512
    unsigned int inode_start; // LBA начала таблицы inode
    unsigned int inode_count; // число записей
    unsigned int data_start;  // LBA начала области данных
    unsigned int cluster_size;// размер кластера в секторах
    unsigned int cluster_count;
    unsigned int next_start;  // LBA таблицы next[]
} __attribute__((packed)) msfs_super_t;

// Inode-запись
typedef struct {
    char          name[MSFS_NAME_LEN];
    unsigned char type;       // 0/1/2
    unsigned char reserved;
    unsigned int  size;       // размер файла в байтах
    unsigned int  first_cluster;
    unsigned int  parent;     // inode родителя (0 для корня)
    unsigned int  next[MSFS_MAX_CHAIN]; // цепочка кластеров
    unsigned char pad[16];    // выравнивание до 64 байт
} __attribute__((packed)) msfs_inode_t;

// Кэш загруженных данных
static msfs_super_t g_super;
static msfs_inode_t g_inodes[MSFS_MAX_INODES];
static int g_loaded = 0;

// ============================================================
// Низкоуровневые helpers
// ============================================================

static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int str_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static void str_cpy_n(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// Читает суперблок с диска (LBA 0 — начало области MSFS).
// В нашем случае MSFS живёт прямо с сектора 0 (как и FAT-образ).
static int msfs_read_super(void) {
    unsigned char buf[SECTOR_SIZE];
    ata_read_sector(0, buf);
    msfs_super_t* s = (msfs_super_t*)buf;
    if (s->magic[0] != 'M' || s->magic[1] != 'S' ||
        s->magic[2] != 'F' || s->magic[3] != 'S') {
        return 0;
    }
    // Копируем поля (буфер локальный)
    g_super.version       = s->version;
    g_super.sector_size   = s->sector_size;
    g_super.inode_start   = s->inode_start;
    g_super.inode_count   = s->inode_count;
    g_super.data_start    = s->data_start;
    g_super.cluster_size  = s->cluster_size;
    g_super.cluster_count = s->cluster_count;
    g_super.next_start    = s->next_start;
    g_super.magic[0]='M'; g_super.magic[1]='S';
    g_super.magic[2]='F'; g_super.magic[3]='S';
    return 1;
}

// Загружает всю таблицу inode в память.
static void msfs_load_inodes(void) {
    unsigned int total = g_super.inode_count;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE; // 8
    unsigned int loaded = 0;
    unsigned int lba = g_super.inode_start;
    while (loaded < total) {
        unsigned char buf[SECTOR_SIZE];
        ata_read_sector(lba, buf);
        for (unsigned int i = 0; i < per_sector && loaded < total; i++) {
            msfs_inode_t* src = (msfs_inode_t*)&buf[i * MSFS_INODE_SIZE];
            // Копируем побайтово (packed-структура)
            unsigned char* d = (unsigned char*)&g_inodes[loaded];
            unsigned char* s = (unsigned char*)src;
            for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) d[k] = s[k];
            loaded++;
        }
        lba++;
    }
    g_loaded = 1;
}

// Сохраняет таблицу inode обратно на диск.
static void msfs_save_inodes(void) {
    unsigned int total = g_super.inode_count;
    unsigned int per_sector = SECTOR_SIZE / MSFS_INODE_SIZE;
    unsigned int saved = 0;
    unsigned int lba = g_super.inode_start;
    while (saved < total) {
        unsigned char buf[SECTOR_SIZE];
        for (unsigned int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
        for (unsigned int i = 0; i < per_sector && saved < total; i++) {
            unsigned char* s = (unsigned char*)&g_inodes[saved];
            unsigned char* d = (unsigned char*)&buf[i * MSFS_INODE_SIZE];
            for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) d[k] = s[k];
            saved++;
        }
        ata_write_sector(lba, buf);
        lba++;
    }
}

// ============================================================
// Публичная инициализация (вызывается из vfs_init)
// ============================================================

// Пытается определить MSFS на диске.
int msfs_probe(void) {
    if (!msfs_read_super()) return 0;
    if (g_super.sector_size != SECTOR_SIZE) return 0;
    if (g_super.inode_count == 0 || g_super.inode_count > MSFS_MAX_INODES) return 0;
    msfs_load_inodes();
    return 1;
}

// Форматирует диск как MSFS (создаёт пустую ФС).
void msfs_format(void) {
    unsigned char buf[SECTOR_SIZE];

    // Параметры по умолчанию
    g_super.version       = MSFS_VERSION;
    g_super.sector_size   = SECTOR_SIZE;
    g_super.inode_start   = 1;
    g_super.inode_count   = MSFS_MAX_INODES;
    g_super.cluster_size  = 1;                 // 1 сектор на кластер
    g_super.cluster_count = 8192;              // ~4 МБ данных
    // next[]-таблица идёт после inode-таблицы
    unsigned int inode_sectors = (MSFS_MAX_INODES * MSFS_INODE_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_super.next_start    = g_super.inode_start + inode_sectors;
    unsigned int next_sectors = (g_super.cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_super.data_start    = g_super.next_start + next_sectors;

    // Пишем суперблок
    for (int k = 0; k < SECTOR_SIZE; k++) buf[k] = 0;
    msfs_super_t* s = (msfs_super_t*)buf;
    s->magic[0]='M'; s->magic[1]='S'; s->magic[2]='F'; s->magic[3]='S';
    s->version       = g_super.version;
    s->sector_size   = g_super.sector_size;
    s->inode_start   = g_super.inode_start;
    s->inode_count   = g_super.inode_count;
    s->data_start    = g_super.data_start;
    s->cluster_size  = g_super.cluster_size;
    s->cluster_count = g_super.cluster_count;
    s->next_start    = g_super.next_start;
    ata_write_sector(0, buf);

    // Обнуляем таблицу inode
    for (int i = 0; i < MSFS_MAX_INODES; i++) {
        unsigned char* p = (unsigned char*)&g_inodes[i];
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
        g_inodes[i].type = MSFS_TYPE_FREE;
    }

    // Корневой каталог
    str_cpy_n(g_inodes[MSFS_ROOT_INODE].name, "/", MSFS_NAME_LEN);
    g_inodes[MSFS_ROOT_INODE].type = MSFS_TYPE_DIR;
    g_inodes[MSFS_ROOT_INODE].size = 0;
    g_inodes[MSFS_ROOT_INODE].first_cluster = 0xFFFFFFFF;
    g_inodes[MSFS_ROOT_INODE].parent = MSFS_ROOT_INODE;
    for (int i = 0; i < MSFS_MAX_CHAIN; i++) g_inodes[MSFS_ROOT_INODE].next[i] = 0xFFFFFFFF;

    msfs_save_inodes();

    // Обнуляем таблицу next[]: все кластеры помечаются как свободные
    // (0xFFFFFFFF). Без этого после форматирования в next[] остаётся
    // мусор со старого диска, и msfs_alloc_cluster может ложно счесть
    // свободные кластеры занятыми (или выдать битую цепочку).
    {
        unsigned char nbuf[SECTOR_SIZE];
        for (unsigned int k = 0; k < SECTOR_SIZE; k++) nbuf[k] = 0xFF;
        unsigned int next_sectors = (g_super.cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        for (unsigned int s = 0; s < next_sectors; s++)
            ata_write_sector(g_super.next_start + s, nbuf);
    }

    g_loaded = 1;
}

// ============================================================
// Поиск / выделение inode
// ============================================================

// Ищет inode по имени в каталоге parent. Возвращает индекс или -1.
static int msfs_find_in(int parent, const char* name) {
    for (unsigned int i = 0; i < g_super.inode_count; i++) {
        if (g_inodes[i].type == MSFS_TYPE_FREE) continue;
        if ((unsigned int)g_inodes[i].parent != (unsigned int)parent) continue;
        if (str_eq(g_inodes[i].name, name)) return (int)i;
    }
    return -1;
}

// Находит свободный inode. Возвращает индекс или -1.
static int msfs_alloc_inode(void) {
    // 0 всегда корень, начинаем с 1
    for (unsigned int i = 1; i < g_super.inode_count; i++) {
        if (g_inodes[i].type == MSFS_TYPE_FREE) return (int)i;
    }
    return -1;
}

// Выделяет свободный кластер данных. Возвращает номер или -1.
static int msfs_alloc_cluster(void) {
    unsigned char buf[SECTOR_SIZE];
    unsigned int idx = 0;
    unsigned int lba = g_super.next_start;
    // next[] — массив uint32; читаем сектор за сектором
    for (unsigned int s = 0; s < (g_super.cluster_count * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE; s++) {
        ata_read_sector(lba + s, buf);
        unsigned int* n = (unsigned int*)buf;
        for (int i = 0; i < SECTOR_SIZE / 4; i++) {
            if (idx >= g_super.cluster_count) return -1;
            if (n[i] == 0xFFFFFFFF) {
                // Помечаем как конец цепочки
                n[i] = 0xFFFFFFFE;
                ata_write_sector(lba + s, buf);
                return (int)idx;
            }
            idx++;
        }
    }
    return -1;
}

// Освобождает кластер (ставит 0xFFFFFFFF в next[]).
static void msfs_free_cluster(unsigned int cluster) {
    unsigned char buf[SECTOR_SIZE];
    unsigned int lba = g_super.next_start + (cluster * 4) / SECTOR_SIZE;
    unsigned int off = (cluster * 4) % SECTOR_SIZE;
    ata_read_sector(lba, buf);
    unsigned int* n = (unsigned int*)&buf[off];
    *n = 0xFFFFFFFF;
    ata_write_sector(lba, buf);
}

// ============================================================
// Разрешение пути
// ============================================================

// Вспомогательная: разбить путь и найти inode.
// base — стартовый каталог (обычно текущий).
// Возвращает индекс inode или -1.
static int msfs_resolve(int base, const char* path) {
    if (path == 0 || path[0] == 0) return base;

    int cur = base;
    int i = 0;
    // Абсолютный путь начинается с '/'
    if (path[0] == '/') { cur = MSFS_ROOT_INODE; i = 1; }

    char comp[MSFS_NAME_LEN];
    while (path[i]) {
        // Пропускаем слэши
        while (path[i] == '/') i++;
        if (path[i] == 0) break;

        // Собираем компонент
        int c = 0;
        while (path[i] && path[i] != '/' && c < MSFS_NAME_LEN - 1) {
            comp[c++] = path[i++];
        }
        comp[c] = 0;

        if (str_eq(comp, ".")) continue;
        if (str_eq(comp, "..")) {
            if (cur != MSFS_ROOT_INODE) cur = (int)g_inodes[cur].parent;
            continue;
        }

        int found = msfs_find_in(cur, comp);
        if (found < 0) return -1;
        cur = found;
    }
    return cur;
}

// Возвращает имя последнего компонента пути в out.
static void msfs_basename(const char* path, char* out) {
    int len = str_len(path);
    int start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    str_cpy_n(out, path + start, MSFS_NAME_LEN);
}

// Разрешает родительский каталог пути. Возвращает индекс каталога,
// а в out кладёт имя последнего компонента. -1 при ошибке.
static int msfs_resolve_parent(int base, const char* path, char* out) {
    msfs_basename(path, out);
    // Отрезаем последний компонент
    char parent_path[FS_PATH_MAX];
    int len = str_len(path);
    int cut = len;
    while (cut > 0 && path[cut - 1] != '/') cut--;
    if (cut > 0) cut--; // убрать сам слэш
    if (cut == 0) {
        // родитель — корень или base
        return (path[0] == '/') ? MSFS_ROOT_INODE : base;
    }
    int i = 0;
    while (i < cut && i < FS_PATH_MAX - 1) { parent_path[i] = path[i]; i++; }
    parent_path[i] = 0;
    return msfs_resolve(base, parent_path);
}

// ============================================================
// Операции MSFS
// ============================================================

// Возвращает индекс inode для пути или -1.
int msfs_lookup(int base, const char* path) {
    if (!g_loaded) return -1;
    return msfs_resolve(base, path);
}

// Проверяет, является ли inode каталогом.
int msfs_is_dir(int inode) {
    if (inode < 0 || (unsigned int)inode >= g_super.inode_count) return 0;
    return g_inodes[inode].type == MSFS_TYPE_DIR;
}

// Читает файл целиком в буфер. Возвращает размер или -1.
int msfs_read_buf(int base, const char* path, unsigned char* out, unsigned int max) {
    int inode = msfs_resolve(base, path);
    if (inode < 0 || g_inodes[inode].type != MSFS_TYPE_FILE) return -1;
    unsigned int total = g_inodes[inode].size;
    unsigned int remaining = total;
    if (remaining > max) remaining = max;
    unsigned int cluster = g_inodes[inode].first_cluster;
    unsigned int pos = 0;
    unsigned char buf[SECTOR_SIZE];
    while (remaining > 0 && cluster != 0xFFFFFFFF) {
        ata_read_sector(g_super.data_start + cluster, buf);
        unsigned int chunk = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
        for (unsigned int i = 0; i < chunk; i++) out[pos++] = buf[i];
        remaining -= chunk;
        if (remaining == 0) break;
        unsigned int lba = g_super.next_start + (cluster * 4) / SECTOR_SIZE;
        unsigned int off = (cluster * 4) % SECTOR_SIZE;
        ata_read_sector(lba, buf);
        unsigned int next = *(unsigned int*)&buf[off];
        if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
        cluster = next;
    }
    // Возвращаем число РЕАЛЬНО прочитанных байт (может быть меньше
    // размера файла, если он не влез в буфер вызывающего).
    (void)total;
    return (int)pos;
}

// Выводит содержимое каталога.
void msfs_list(int base, const char* path) {
    int dir = msfs_resolve(base, path);
    if (dir < 0) { print_string("msfs: no such directory\n"); return; }
    if (g_inodes[dir].type != MSFS_TYPE_DIR) {
        print_string("msfs: not a directory\n");
        return;
    }

    print_string("Directory: ");
    print_string(path && path[0] ? path : "/");
    print_string("\n");
    print_string("------------------\n");

    int count = 0;
    for (unsigned int i = 0; i < g_super.inode_count; i++) {
        if (g_inodes[i].type == MSFS_TYPE_FREE) continue;
        if ((int)i == dir) continue;
        if ((unsigned int)g_inodes[i].parent != (unsigned int)dir) continue;

        print_string("  ");
        print_string(g_inodes[i].name);
        if (g_inodes[i].type == MSFS_TYPE_DIR) {
            print_string("/");
        } else {
            // выравнивание
            int nl = str_len(g_inodes[i].name);
            for (int k = nl; k < 20; k++) put_char(' ');
            print_int(g_inodes[i].size);
            print_string(" bytes");
        }
        put_char('\n');
        count++;
    }
    if (count == 0) print_string("  (empty)\n");
    print_string("------------------\n");
}

// Выводит содержимое файла.
void msfs_read(int base, const char* path) {
    int inode = msfs_resolve(base, path);
    if (inode < 0 || g_inodes[inode].type != MSFS_TYPE_FILE) {
        print_string("msfs: no such file\n");
        return;
    }

    unsigned int remaining = g_inodes[inode].size;
    unsigned int cluster = g_inodes[inode].first_cluster;
    unsigned char buf[SECTOR_SIZE];

    while (remaining > 0 && cluster != 0xFFFFFFFF) {
        ata_read_sector(g_super.data_start + cluster, buf);
        unsigned int chunk = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
        for (unsigned int i = 0; i < chunk; i++) put_char((char)buf[i]);
        remaining -= chunk;

        // Следующий кластер из next[]
        unsigned int lba = g_super.next_start + (cluster * 4) / SECTOR_SIZE;
        unsigned int off = (cluster * 4) % SECTOR_SIZE;
        ata_read_sector(lba, buf);
        unsigned int next = *(unsigned int*)&buf[off];
        if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
        cluster = next;
    }
    put_char('\n');
}

// Создаёт каталог.
int msfs_mkdir(int base, const char* path) {
    char name[MSFS_NAME_LEN];
    int parent = msfs_resolve_parent(base, path, name);
    if (parent < 0 || !msfs_is_dir(parent)) {
        print_string("msfs: invalid parent\n");
        return 0;
    }
    if (msfs_find_in(parent, name) >= 0) {
        print_string("msfs: already exists\n");
        return 0;
    }
    int idx = msfs_alloc_inode();
    if (idx < 0) { print_string("msfs: no free inode\n"); return 0; }

    msfs_inode_t* n = &g_inodes[idx];
    unsigned char* p = (unsigned char*)n;
    for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
    str_cpy_n(n->name, name, MSFS_NAME_LEN);
    n->type = MSFS_TYPE_DIR;
    n->size = 0;
    n->first_cluster = 0xFFFFFFFF;
    n->parent = (unsigned int)parent;
    for (int i = 0; i < MSFS_MAX_CHAIN; i++) n->next[i] = 0xFFFFFFFF;

    msfs_save_inodes();
    return 1;
}

// Записывает файл (перезаписывает, если существует).
void msfs_write_buf(int base, const char* path, const unsigned char* content, unsigned int len) {
    char name[MSFS_NAME_LEN];
    int parent = msfs_resolve_parent(base, path, name);
    if (parent < 0 || !msfs_is_dir(parent)) {
        print_string("msfs: invalid parent\n");
        return;
    }

    if (len == 0) { print_string("msfs: empty content\n"); return; }

    // Ищем существующий файл
    int idx = msfs_find_in(parent, name);
    if (idx >= 0 && g_inodes[idx].type != MSFS_TYPE_FILE) {
        print_string("msfs: name is a directory\n");
        return;
    }
    if (idx < 0) {
        idx = msfs_alloc_inode();
        if (idx < 0) { print_string("msfs: no free inode\n"); return; }
        msfs_inode_t* n = &g_inodes[idx];
        unsigned char* p = (unsigned char*)n;
        for (unsigned int k = 0; k < MSFS_INODE_SIZE; k++) p[k] = 0;
        str_cpy_n(n->name, name, MSFS_NAME_LEN);
        n->type = MSFS_TYPE_FILE;
        n->parent = (unsigned int)parent;
        n->first_cluster = 0xFFFFFFFF;
        for (int i = 0; i < MSFS_MAX_CHAIN; i++) n->next[i] = 0xFFFFFFFF;
    } else {
        // Освобождаем старую цепочку
        unsigned int c = g_inodes[idx].first_cluster;
        while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = g_super.next_start + (c * 4) / SECTOR_SIZE;
            unsigned int off = (c * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            unsigned int next = *(unsigned int*)&buf[off];
            msfs_free_cluster(c);
            if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
            c = next;
        }
        g_inodes[idx].first_cluster = 0xFFFFFFFF;
    }

    // Выделяем цепочку
    unsigned int written = 0;
    unsigned int prev = 0xFFFFFFFF;
    unsigned int first = 0xFFFFFFFF;
    while (written < len) {
        int cl = msfs_alloc_cluster();
        if (cl < 0) {
            // Не хватило места: освобождаем уже выделенную цепочку,
            // чтобы кластеры не утекли, и выходим без изменения inode.
            print_string("msfs: no free space\n");
            unsigned int c = first;
            while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
                unsigned char tmp[SECTOR_SIZE];
                unsigned int lba = g_super.next_start + (c * 4) / SECTOR_SIZE;
                unsigned int off = (c * 4) % SECTOR_SIZE;
                ata_read_sector(lba, tmp);
                unsigned int next = *(unsigned int*)&tmp[off];
                msfs_free_cluster(c);
                if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
                c = next;
            }
            g_inodes[idx].first_cluster = 0xFFFFFFFF;
            g_inodes[idx].size = 0;
            msfs_save_inodes();
            return;
        }
        unsigned int cluster = (unsigned int)cl;
        if (first == 0xFFFFFFFF) first = cluster;
        if (prev != 0xFFFFFFFF) {
            // связать prev -> cluster
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = g_super.next_start + (prev * 4) / SECTOR_SIZE;
            unsigned int off = (prev * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            *(unsigned int*)&buf[off] = cluster;
            ata_write_sector(lba, buf);
        }

        // Пишем данные
        unsigned char data[SECTOR_SIZE];
        for (int k = 0; k < SECTOR_SIZE; k++) data[k] = 0;
        unsigned int chunk = len - written < SECTOR_SIZE ? len - written : SECTOR_SIZE;
        for (unsigned int k = 0; k < chunk; k++) data[k] = content[written + k];
        ata_write_sector(g_super.data_start + cluster, data);
        written += chunk;
        prev = cluster;
    }

    g_inodes[idx].first_cluster = first;
    g_inodes[idx].size = len;
    msfs_save_inodes();

    print_string("msfs: wrote ");
    print_int((int)len);
    print_string(" bytes to ");
    print_string(path);
    print_string("\n");
}

// Обёртка: запись C-строки
void msfs_write(int base, const char* path, const char* content) {
    unsigned int len = (unsigned int)str_len(content);
    msfs_write_buf(base, path, (const unsigned char*)content, len);
}

// Удаляет файл или пустой каталог.
int msfs_remove(int base, const char* path) {
    int inode = msfs_resolve(base, path);
    if (inode < 0 || inode == MSFS_ROOT_INODE) {
        print_string("msfs: no such file\n");
        return 0;
    }
    if (g_inodes[inode].type == MSFS_TYPE_DIR) {
        // проверяем, пуст ли
        for (unsigned int i = 0; i < g_super.inode_count; i++) {
            if (g_inodes[i].type != MSFS_TYPE_FREE &&
                (unsigned int)g_inodes[i].parent == (unsigned int)inode) {
                print_string("msfs: directory not empty\n");
                return 0;
            }
        }
    } else {
        // освобождаем цепочку
        unsigned int c = g_inodes[inode].first_cluster;
        while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = g_super.next_start + (c * 4) / SECTOR_SIZE;
            unsigned int off = (c * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            unsigned int next = *(unsigned int*)&buf[off];
            msfs_free_cluster(c);
            if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
            c = next;
        }
    }
    g_inodes[inode].type = MSFS_TYPE_FREE;
    msfs_save_inodes();
    return 1;
}

// (рекурсивное удаление удалено при откате)
#if 0
static void msfs_purge_inode(int inode) {
    if (g_inodes[inode].type == MSFS_TYPE_DIR) {
        // сначала рекурсивно удаляем всех детей
        for (unsigned int i = 0; i < g_super.inode_count; i++) {
            if (g_inodes[i].type != MSFS_TYPE_FREE &&
                (unsigned int)g_inodes[i].parent == (unsigned int)inode &&
                (int)i != inode) {
                msfs_purge_inode((int)i);
            }
        }
    } else if (g_inodes[inode].type == MSFS_TYPE_FILE) {
        // освобождаем цепочку кластеров
        unsigned int c = g_inodes[inode].first_cluster;
        while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
            unsigned char buf[SECTOR_SIZE];
            unsigned int lba = g_super.next_start + (c * 4) / SECTOR_SIZE;
            unsigned int off = (c * 4) % SECTOR_SIZE;
            ata_read_sector(lba, buf);
            unsigned int next = *(unsigned int*)&buf[off];
            msfs_free_cluster(c);
            if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
            c = next;
        }
    }
    g_inodes[inode].type = MSFS_TYPE_FREE;
}

// Рекурсивное удаление: удаляет файл или каталог со всем содержимым.
int msfs_remove_recursive(int base, const char* path) {
    int inode = msfs_resolve(base, path);
    if (inode < 0 || inode == MSFS_ROOT_INODE) {
        print_string("rm: no such file or directory\n");
        return 0;
    }
    msfs_purge_inode(inode);
    msfs_save_inodes();
    return 1;
}

// Очищает всю ФС (все inode кроме корня). Для rm -rf /.
int msfs_wipe_all(void) {
    for (unsigned int i = 1; i < g_super.inode_count; i++) {
        if (g_inodes[i].type != MSFS_TYPE_FREE) {
            if (g_inodes[i].type == MSFS_TYPE_FILE) {
                unsigned int c = g_inodes[i].first_cluster;
                while (c != 0xFFFFFFFF && c != 0xFFFFFFFE) {
                    unsigned char buf[SECTOR_SIZE];
                    unsigned int lba = g_super.next_start + (c * 4) / SECTOR_SIZE;
                    unsigned int off = (c * 4) % SECTOR_SIZE;
                    ata_read_sector(lba, buf);
                    unsigned int next = *(unsigned int*)&buf[off];
                    msfs_free_cluster(c);
                    if (next == 0xFFFFFFFE || next == 0xFFFFFFFF) break;
                    c = next;
                }
            }
            g_inodes[i].type = MSFS_TYPE_FREE;
        }
    }
    msfs_save_inodes();
    return 1;
}
#endif
