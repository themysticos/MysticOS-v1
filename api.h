// api.h - API ядра MysticOS для пользовательских ELF-программ (v2)
// Программа получает указатель на mystic_api_t в _start(api*).
//
// ВАЖНО: поля, добавленные в v2, идут ПОСЛЕ старых полей, поэтому
// программы, собранные под v1, продолжают работать без пересборки.
// Проверяйте api->version: v1 = 10 полей, v2 = расширенный набор.
#ifndef MYSTIC_API_H
#define MYSTIC_API_H

#define MYSTIC_API_VERSION 3

// --- Коды ошибок VFS/API ---
#define MYSTIC_OK          0
#define MYSTIC_ENOENT     -1   // нет файла/каталога
#define MYSTIC_EIO        -2   // ошибка ввода-вывода
#define MYSTIC_ENOSPC     -3   // нет места
#define MYSTIC_EINVAL     -4   // неверный аргумент
#define MYSTIC_ENOTDIR    -5   // не каталог
#define MYSTIC_EISDIR     -6   // это каталог

// --- Типы ФС (совпадают с fs_type_t ядра) ---
#define MYSTIC_FS_UNKNOWN 0
#define MYSTIC_FS_FAT12   1
#define MYSTIC_FS_FAT16   2
#define MYSTIC_FS_FAT32   3
#define MYSTIC_FS_MSFS    4

// Информация об устройстве
typedef struct {
    char          name[16];
    unsigned char type;        // 0=ATA, 1=NVMe, 2=RAM
    unsigned int  size_sectors;
} mystic_device_t;

// Информация о памяти
typedef struct {
    unsigned int heap_total;
    unsigned int heap_used;
    unsigned int heap_free;
} mystic_meminfo_t;

typedef struct {
    unsigned int version;

    // ============ v1: вывод ============
    void (*put_char)(char c);
    void (*print_string)(const char* s);
    void (*print_int)(int v);
    void (*print_hex)(unsigned int v);
    void (*clear_screen)(void);
    void (*set_color)(unsigned char fg, unsigned char bg);

    // ============ v1: ввод ============
    char (*get_char)(void);
    const char* (*getcwd)(void);

    // ============ v2: экран ============
    void (*set_cursor)(int x, int y);   // 0..79, 0..24
    int  (*get_cursor_x)(void);
    int  (*get_cursor_y)(void);
    int  (*screen_width)(void);         // 80
    int  (*screen_height)(void);        // 25
    void (*scroll_up)(void);            // прокрутить экран на строку

    // ============ v2: ввод ============
    int  (*key_available)(void);        // 1, если есть непрочитанный скан-код
    int  (*read_line)(char* buf, int max); // блокирующий ввод строки, возвр. длину

    // ============ v2: строки (безопасные, с ограничением) ============
    int    (*strlen)(const char* s);
    int    (*strcmp)(const char* a, const char* b);
    void   (*strcpy)(char* dst, const char* src, int max);
    void   (*strcat)(char* dst, const char* src, int max);
    int    (*atoi)(const char* s);
    void   (*itoa)(int v, char* buf, int base);
    void   (*memset)(void* dst, int val, unsigned int n);
    void   (*memcpy)(void* dst, const void* src, unsigned int n);
    char   (*toupper)(char c);
    char   (*tolower)(char c);

    // ============ v2: файловая система ============
    int  (*chdir)(const char* path);                 // MYSTIC_OK / ошибка
    void (*ls)(const char* path);
    void (*cat)(const char* path);
    int  (*write_file)(const char* path, const char* content);
    int  (*mkdir)(const char* path);
    int  (*rm)(const char* path);
    int  (*read_file)(const char* path, unsigned char* buf, unsigned int max);
    int  (*file_exists)(const char* path);           // 1/0
    int  (*fs_type)(void);                           // MYSTIC_FS_*
    const char* (*fs_type_name)(void);

    // ============ v2: память (куча ядра) ============
    void* (*malloc)(unsigned int size);
    void  (*free)(void* ptr);
    void  (*mem_info)(mystic_meminfo_t* out);

    // ============ v2: система ============
    unsigned int (*uptime_ms)(void);
    void (*reboot)(void);
    int  (*devices_count)(void);
    int  (*device_info)(int idx, mystic_device_t* out);
    void (*yield)(void);                             // отдать кадр (обновить экран)

    // ============ v3: завершение программы ============
    // Немедленно завершает программу с данным кодом (через int 0x80),
    // возвращая управление в exec_run. Не возвращается!
    void (*exit)(int code);

    // Приостановить программу на ms миллисекунд (по тикам PIT).
    void (*sleep)(unsigned int ms);
} mystic_api_t;

#endif
