// fat.h — путь-ориентированный API драйвера FAT12/FAT16 для MysticOS
#ifndef FAT_H
#define FAT_H

// Все функции принимают текущий каталог как base_dir:
//   0    — корневой каталог (FAT16, фиксированная область)
//   >0   — первый кластер подкаталога
// Пути могут быть абсолютными ("/dir/file") или относительными.

int  fat_find(int base_dir, const char* path);      // 1 если найден, 0 если нет
void fat_list(int base_dir, const char* path);      // печатает содержимое каталога
void fat_read(int base_dir, const char* path);      // печатает содержимое файла
void fat_write_path(int base_dir, const char* path, const char* content);
int  fat_mkdir(int base_dir, const char* path);     // 1 при успехе
int  fat_cd(const char* path, unsigned int* out_cluster); // абсолютный путь; 1 при успехе

#endif
