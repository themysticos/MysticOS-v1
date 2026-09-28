// fs.h — единый API для всех ФС MysticOS
#ifndef FS_H
#define FS_H

typedef enum { FS_UNKNOWN = 0, FS_FAT12, FS_FAT16, FS_FAT32, FS_MSFS } fs_type_t;

// Максимальная длина пути (включая ведущий '/' и завершающий '\0')
#define FS_PATH_MAX 128

// --- Инициализация / определение типа ФС ---
void vfs_init(void);
fs_type_t vfs_get_type(void);
const char* vfs_type_name(void);

// --- Текущий рабочий каталог ---
// Возвращает текущий путь (начинается с '/', без завершающего слэша,
// кроме корня, который равен "/").
const char* vfs_getcwd(void);
// Меняет текущий каталог. Возвращает 1 при успехе, 0 при ошибке.
int vfs_cd(const char* path);

// --- Операции над файлами/каталогами ---
// Все пути могут быть абсолютными ("/dir/file") или относительными
// ("dir/file", "../file"). Разделитель — '/'.
void vfs_ls(const char* path);          // path == NULL или "" => текущий каталог
void vfs_cat(const char* path);
void vfs_write(const char* path, const char* content);
int  vfs_mkdir(const char* path);       // возвращает 1 при успехе
int  vfs_rm(const char* path);          // удаление файла/пустого каталога
void vfs_format(void);                  // форматировать диск как MSFS
// Читает весь файл в buf (до max байт). Возвращает размер или -1.
int  vfs_read_file(const char* path, unsigned char* buf, unsigned int max);

#endif
