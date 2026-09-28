# Mystic-OS

> ⚠️ **Эта версия ОС заброшена и НЕ поддерживается.**
> В будущем будут обновляться только крупные баги.
> Актуальная разработка ведётся в **MysticOS 2**.
>
> *This version of the OS is abandoned and NOT supported — only major bugs will be updated in the future.*

Небольшая 32-битная hobby-ОС для x86, написанная на C и NASM.
Загружается собственным bootloader'ом, работает в защищённом режиме,
имеет текстовую VGA-оболочку, драйверы ATA (PIO, LBA28), сканер PCI
и слой VFS с драйвером FAT12/FAT16.

## Возможности

- **Bootloader** (`bootloader.asm`) — грузит ядро с диска.
- **Ядро** (`kernel.c`):
  - VGA-текстовый режим 80x25, цвета, скроллинг, курсор;
  - ввод с клавиатуры (PS/2, скан-код set 1);
  - команды: `help`, `clear`, `info`, `ver`, `echo`, `mystic`,
    `devices`, `pci`, `reboot`, `ls`, `cat`, `write`;
  - менеджер блочных устройств (RAM-диск + найденные ATA/NVMe);
  - полное сканирование шин PCI.
- **ATA** — чтение и запись секторов (PIO, LBA28).
- **VFS** (`vfs.c`, `fs.h`) — автоопределение ФС и единый API
  (`vfs_ls`, `vfs_cat`, `vfs_write`) поверх драйверов.
- **FAT12/FAT16** (`fat.c`) — чтение каталога, вывод файлов,
  многокластерная запись с корректной обработкой границ секторов FAT12.
- **MSFS** (`msfs.c`) — собственная ФС MysticOS: суперблок,
  таблица inode, цепочки кластеров; чтение, запись, каталоги,
  удаление.

## Сборка

Требуется Linux/WSL (или MSYS2) с инструментами:

- `nasm`
- `gcc` (с поддержкой `-m32`, т.е. gcc-multilib)
- `ld`, `objcopy` (binutils)
- `mkfs.fat` (пакет `dosfstools`)
- `mtools` (для `mcopy`)
- `qemu-system-x86_64` (для запуска)

Сборка образа и запуск в QEMU:

```sh
make          # собрать mysticos.img
make run      # запустить в QEMU
make clean    # очистить артефакты
```

## Структура

| Файл            | Назначение                                  |
|-----------------|---------------------------------------------|
| `bootloader.asm`| загрузчик (реальный режим)                  |
| `entry.asm`     | точка входа ядра (переход в protected mode) |
| `linker.ld`     | скрипт линковки                             |
| `kernel.c`      | ядро: VGA, клавиатура, ATA, PCI, оболочка   |
| `fat.c`/`fat.h` | драйвер FAT12/FAT16                         |
| `vfs.c`         | слой виртуальной ФС                         |
| `fs.h`          | общий API/типы ФС                           |
| `msfs.c`        | MSFS — собственная файловая система          |

## Статус

⚠️ **Заброшена / Abandoned.** Разработка остановлена на v0.2.0.
Обновляются только крупные баги. Новая версия — **MysticOS 2**.

*Abandoned at v0.2.0. Only major bug fixes. See MysticOS 2 for current work.*
