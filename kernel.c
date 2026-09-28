// kernel.c - MysticOS с расширенным PCI-сканером

#define VIDEO_MEMORY 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GREY 7
#define COLOR_DARK_GREY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

#include "fs.h"
#include "api.h"
#include "idt.h"
#include "pic.h"
#include "vbe.h"

// --- Исполняемые файлы (exec.c) ---
int exec_run(const unsigned char* image, unsigned int size, const mystic_api_t* api);

const mystic_api_t* kernel_api(void);

// Прототипы, используемые ATA-функциями (их определения ниже в файле).
void print_string(const char* str);
void put_char(char c);


void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
void outw(unsigned short port, unsigned short value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
unsigned short inw(unsigned short port) {
    unsigned short result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
void outl(unsigned short port, unsigned int value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
unsigned int inl(unsigned short port) {
    unsigned int result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static int g_ata_drive = 0;  // 0 = primary master (hda), 1 = primary slave (hdb)
void ata_select(int drive) { g_ata_drive = drive & 1; }
int ata_current(void) { return g_ata_drive; }

void ata_read_sector(unsigned int lba, unsigned char* buffer) {
    // Перед новой командой дождаться, пока контроллер освободится
    // (BSY=0). Без этого повторные чтения подряд бьют данные.
    {
        unsigned int w = 0;
        while ((inb(0x1F7) & 0x80)) { if (++w >= 100000000u) break; }
    }

    outb(0x1F6, 0xE0 | (g_ata_drive << 4) | ((lba >> 24) & 0x0F));

    // 400ns delay: 4 чтения альтернативного статуса (порт 0x3F6).
    inb(0x3F6); inb(0x3F6); inb(0x3F6); inb(0x3F6);

    outb(0x1F2, 1);
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));
    outb(0x1F7, 0x20);

    // Ждём готовности с таймаутом, чтобы не зависнуть навсегда,
    // если диск отсутствует или не отвечает. Статус читаем ОДИН раз
    // за итерацию (двойной inb(0x1F7) мог дать гонку по биту DRQ),
    // и таймаут щедрый — порт I/O медленный, а диск может быть занят.
    for (unsigned int timeout = 0; ; timeout++) {
        unsigned char status = inb(0x1F7);
        if (!(status & 0x80) && (status & 0x08)) break;   // BSY=0, DRQ=1
        if (status & 0x01) {                               // ERR
            print_string("ata_read_sector: device error\n");
            return;
        }
        if (timeout >= 100000000u) {
            print_string("ata_read_sector: timeout\n");
            return;
        }
    }

    for (int i = 0; i < 256; i++) {
        unsigned short data = inw(0x1F0);
        buffer[i * 2] = data & 0xFF;
        buffer[i * 2 + 1] = (data >> 8) & 0xFF;
    }
}

// Физическая реализация записи сектора ATA LBA28 PIO для линкера
void ata_write_sector(unsigned int lba, const unsigned char* buffer) {
    // 0. Дождаться, пока контроллер освободится (BSY=0).
    {
        unsigned int w = 0;
        while ((inb(0x1F7) & 0x80)) { if (++w >= 100000000u) break; }
    }

    // 1. Выбираем диск и передаем старшие 4 бита LBA адреса
    outb(0x1F6, 0xE0 | (g_ata_drive << 4) | ((lba >> 24) & 0x0F));

    // 400ns delay
    inb(0x3F6); inb(0x3F6); inb(0x3F6); inb(0x3F6);

    // 2. Указываем количество секторов для записи (1 сектор)
    outb(0x1F2, 1);

    // 3. Передаем оставшиеся биты LBA адреса в порты контроллера
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));

    // 4. Посылаем команду 0x30 (Write Sectors)
    outb(0x1F7, 0x30);

    // 5. Ждем готовности контроллера (BSY=0, DRQ=1) с таймаутом.
    //    Статус читаем один раз за итерацию, таймаут щедрый.
    for (unsigned int timeout = 0; ; timeout++) {
        unsigned char status = inb(0x1F7);
        if (!(status & 0x80) && (status & 0x08)) break;
        if (status & 0x01) {
            print_string("ata_write_sector: device error (DRQ)\n");
            return;
        }
        if (timeout >= 100000000u) {
            print_string("ata_write_sector: timeout (DRQ)\n");
            return;
        }
    }

    // 6. Выплескиваем 256 слов (512 байт) из буфера Си напрямую в порт данных 0x1F0
    for (int i = 0; i < 256; i++) {
        unsigned short data = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw(0x1F0, data);
    }

    // 7. Ждем, пока контроллер полностью завершит внутреннюю запись на носитель
    for (unsigned int timeout = 0; ; timeout++) {
        unsigned char status = inb(0x1F7);
        if (!(status & 0x80)) break;                      // BSY=0
        if (timeout >= 100000000u) {
            print_string("ata_write_sector: timeout (flush)\n");
            return;
        }
    }
}

void update_cursor(void);
void put_char(char c);
void print_string(const char* str);
void print_int(int num);
void print_hex(unsigned int num);
void clear_screen(void);
void print_line(char c, int length);
void print_header(void);
void print_prompt(void);
void process_command(const char* command);
void read_string(char* buffer, int max_length);
void set_text_color(unsigned char fg, unsigned char bg);
char get_char(void);
int str_compare(const char* s1, const char* s2);
void str_copy(char* dst, const char* src);
void init_device_manager(void);

unsigned char make_color(unsigned char fg, unsigned char bg) {
    return (bg << 4) | fg;
}

typedef struct {
    char character;
    unsigned char color;
} vga_char;

int cursor_x = 0, cursor_y = 0;
unsigned char current_color;
vga_char* video_buffer = (vga_char*) VIDEO_MEMORY;

// VGA-цвет (0-15) -> RGB для графики.
static unsigned int vga_to_rgb(unsigned char c) {
    // Стандартная палитра VGA (16 цветов).
    static const unsigned int pal[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    return pal[c & 0x0F];
}

// Графический режим: вывод через framebuffer.
// Символы 8x8, экран ~ 100x75 символов при 800x600.
static int g_text_fg = 7, g_text_bg = 0;

void set_text_color(unsigned char fg, unsigned char bg) {
    current_color = make_color(fg, bg);
    g_text_fg = fg; g_text_bg = bg;
}

void clear_screen() {
    if (vbe_active()) {
        fb_clear(vga_to_rgb((unsigned char)g_text_bg));
        cursor_x = cursor_y = 0;
        return;
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int idx = y * SCREEN_WIDTH + x;
            video_buffer[idx].character = ' ';
            video_buffer[idx].color = current_color;
        }
    cursor_x = cursor_y = 0;
}
void update_cursor() {
    if (vbe_active()) return;   // курсор VGA в графике не нужен
    unsigned int pos = cursor_y * SCREEN_WIDTH + cursor_x;
    outb(0x3D4, 0x0E); outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
}

// Графические размеры текстовой сетки (8x8 шрифт).
static int gfx_cols(void) { return (int)vbe_width() / 8; }
static int gfx_rows(void) { return (int)vbe_height() / 8; }

void put_char(char c) {
    if (vbe_active()) {
        int cols = gfx_cols(), rows = gfx_rows();
        if (c == '\n') { cursor_x = 0; cursor_y++; }
        else if (c == '\r') cursor_x = 0;
        else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                fb_draw_char(cursor_x*8, cursor_y*8, ' ',
                             vga_to_rgb(g_text_fg), vga_to_rgb(g_text_bg));
            }
        }
        else if (c == '\t') cursor_x = (cursor_x + 8) & ~7;
        else {
            fb_draw_char(cursor_x*8, cursor_y*8, c,
                         vga_to_rgb(g_text_fg), vga_to_rgb(g_text_bg));
            cursor_x++;
        }
        if (cursor_x >= cols) { cursor_x = 0; cursor_y++; }
        if (cursor_y >= rows) {
            // Скролл в графике: пока простая очистка снизу.
            cursor_y = rows - 1;
        }
        return;
    }

    if (c == '\n') { cursor_x = 0; cursor_y++; }
    else if (c == '\r') cursor_x = 0;
    else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            video_buffer[cursor_y * SCREEN_WIDTH + cursor_x].character = ' ';
        }
    }
    else if (c == '\t') cursor_x = (cursor_x + 8) & ~7;
    else {
        video_buffer[cursor_y * SCREEN_WIDTH + cursor_x].character = c;
        video_buffer[cursor_y * SCREEN_WIDTH + cursor_x].color = current_color;
        cursor_x++;
    }
    if (cursor_x >= SCREEN_WIDTH) { cursor_x = 0; cursor_y++; }
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int y = 1; y < SCREEN_HEIGHT; y++)
            for (int x = 0; x < SCREEN_WIDTH; x++)
                video_buffer[(y-1)*SCREEN_WIDTH + x] = video_buffer[y*SCREEN_WIDTH + x];
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            video_buffer[(SCREEN_HEIGHT-1)*SCREEN_WIDTH + x].character = ' ';
            video_buffer[(SCREEN_HEIGHT-1)*SCREEN_WIDTH + x].color = current_color;
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
    update_cursor();
}

void print_string(const char* str) { while (*str) put_char(*str++); }
void print_int(int num) {
    if (num == 0) { put_char('0'); return; }
    char buf[12]; int i = 0, neg = 0;
    if (num < 0) { neg = 1; num = -num; }
    while (num) { buf[i++] = '0' + (num % 10); num /= 10; }
    if (neg) buf[i++] = '-';
    while (i) put_char(buf[--i]);
}
void print_hex(unsigned int num) {
    print_string("0x");
    char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) put_char(h[(num >> i) & 0xF]);
}

unsigned char get_scancode() {
    while (!(inb(0x64) & 1));
    return inb(0x60);
}
char get_char() {
    // Теперь ввод идёт через буфер, наполняемый IRQ1 (см. pic.c).
    return (char)keyboard_getchar();
}
void read_string(char* buffer, int max) {
    keyboard_flush();   // сбросить застрявшие символы от прошлого ввода
    int i = 0;
    while (i < max-1) {
        char c = get_char();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') { if (i) { i--; put_char('\b'); } }
        else if (c >= ' ') { buffer[i++] = c; put_char(c); }
    }
    buffer[i] = 0;
    put_char('\n');
}

void print_line(char c, int len) { while (len--) put_char(c); put_char('\n'); }
void print_header() {
    set_text_color(COLOR_CYAN, COLOR_BLACK);
    print_line('=', 42);
    set_text_color(COLOR_WHITE, COLOR_BLACK);
    for (int i = 0; i < 16; i++) put_char(' ');
    print_string("MysticOS");
    put_char('\n');
    set_text_color(COLOR_CYAN, COLOR_BLACK);
    print_line('=', 42);
}
void print_prompt() {
    set_text_color(COLOR_GREEN, COLOR_BLACK);
    put_char('[');
    set_text_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    print_string(vfs_getcwd());
    set_text_color(COLOR_GREEN, COLOR_BLACK);
    put_char(']');
    set_text_color(COLOR_WHITE, COLOR_BLACK);
    print_string("> ");
}
int str_compare(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}
void str_copy(char* dst, const char* src) { while (*src) *dst++ = *src++; *dst = 0; }

// --------------------- Устройства -----------------------
#define MAX_DEVICES 8
typedef struct {
    unsigned char present, type;   // 0=ATA, 1=NVMe, 2=RAM
    unsigned short io_base;
    unsigned char pci_bus, pci_dev, pci_func;
    unsigned int size_sectors;
    char name[16];
} block_device_t;
block_device_t devices[MAX_DEVICES];
int num_devices = 0;

// --------------------- PCI ------------------------------
unsigned int pci_read_config(unsigned char bus, unsigned char dev, unsigned char func, unsigned char offset) {
    unsigned int addr = 0x80000000 | ((unsigned int)bus << 16) | ((unsigned int)dev << 11) | ((unsigned int)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}
unsigned short pci_get_vendor(unsigned char bus, unsigned char dev, unsigned char func) {
    return pci_read_config(bus, dev, func, 0) & 0xFFFF;
}

// --------------------- ATA detection --------------------
int ata_detect(unsigned short base) {
    outb(base + 6, 0xA0);
    for (int i = 0; i < 4; i++) inb(base + 7);
    unsigned char st = inb(base + 7);
    if (st == 0 || st == 0xFF) return 0;
    outb(base + 6, 0xA0);
    outb(base + 7, 0xEC);
    while (inb(base + 7) & 0x80);
    if (inb(base + 7) == 0) return 0;
    for (int i = 0; i < 256; i++) inw(base);
    return 1;
}
unsigned int ata_get_size(unsigned short base) {
    (void)base;
    return 0x8000;
}

// --------------------- Инициализация устройств ----------
void init_device_manager() {
    // RAM disk
    devices[0].present = 1; devices[0].type = 2;
    devices[0].size_sectors = 8192;
    str_copy(devices[0].name, "ram0");
    num_devices = 1;

    // Сканируем все шины PCI
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                unsigned short vendor = pci_get_vendor(bus, dev, func);
                if (vendor == 0xFFFF) continue;

                unsigned int class_reg = pci_read_config(bus, dev, func, 8);
                unsigned char class_code = (class_reg >> 24) & 0xFF;
                unsigned char subclass = (class_reg >> 16) & 0xFF;

                if (class_code == 0x01) { // Mass Storage
                    if (subclass == 0x01) { // IDE
                        if (ata_detect(0x1F0) && num_devices < MAX_DEVICES) {
                            devices[num_devices].present = 1;
                            devices[num_devices].type = 0;
                            devices[num_devices].io_base = 0x1F0;
                            devices[num_devices].size_sectors = ata_get_size(0x1F0);
                            str_copy(devices[num_devices].name, "hda");
                            num_devices++;
                        }
                        if (ata_detect(0x170) && num_devices < MAX_DEVICES) {
                            devices[num_devices].present = 1;
                            devices[num_devices].type = 0;
                            devices[num_devices].io_base = 0x170;
                            devices[num_devices].size_sectors = ata_get_size(0x170);
                            str_copy(devices[num_devices].name, "hdb");
                            num_devices++;
                        }
                    }
                    else if (subclass == 0x08) { // NVMe
                        if (num_devices < MAX_DEVICES) {
                            devices[num_devices].present = 1;
                            devices[num_devices].type = 1;
                            devices[num_devices].size_sectors = 0;
                            // имя nvme0, nvme1...
                            char nname[16];
                            int nvme_cnt = 0;
                            for (int i = 0; i < num_devices; i++)
                                if (devices[i].type == 1) nvme_cnt++;
                            nname[0] = 'n'; nname[1] = 'v'; nname[2] = 'm'; nname[3] = 'e';
                            nname[4] = '0' + nvme_cnt; nname[5] = 0;
                            str_copy(devices[num_devices].name, nname);
                            devices[num_devices].pci_bus = bus;
                            devices[num_devices].pci_dev = dev;
                            devices[num_devices].pci_func = func;
                            num_devices++;
                        }
                    }
                }
            }
        }
    }
}

// --------------------- Команды ---------------------------
void process_command(const char* cmd) {
    set_text_color(COLOR_YELLOW, COLOR_BLACK);
    if (str_compare(cmd, "help") == 0) {
        print_string("Available commands:\n");
	set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
	print_string("  help            - this message\n");
	print_string("  clear           - clear screen\n");
	print_string("  info            - system info\n");
	print_string("  ver             - version\n");
	print_string("  echo <text>     - print text\n");
	print_string("  mystic          - show ASCII art\n");
	print_string("  devices         - list storage devices\n");
	print_string("  pci             - list PCI devices\n");
	print_string("  reboot          - reboot system\n");
	print_string("  ls [path]       - list files\n");
	print_string("  cat <file>      - print file content\n");
	print_string("  write <f> <txt> - write text to file\n");
	print_string("  cd <path>       - change directory\n");
	print_string("  mkdir <path>    - create directory\n");
	print_string("  rm <path>       - remove file/dir (MSFS)\n");
	print_string("  mkfs            - format device as MSFS\n");
	print_string("  run <file>      - execute ELF program\n");
    }
    else if (cmd[0]=='c' && cmd[1]=='a' && cmd[2]=='t' && (cmd[3]==' ' || cmd[3]=='\0')) {
    if (cmd[3] == '\0') {
        print_string("Usage: cat <filename>\n");
    } else {
        vfs_cat(cmd + 4);
    }
}
    else if (cmd[0]=='w' && cmd[1]=='r' && cmd[2]=='i' && cmd[3]=='t' &&
         cmd[4]=='e' && cmd[5]==' ') {
    char tmp[256];
    int k = 0;
    while (cmd[k] && k < 255) { tmp[k] = cmd[k]; k++; }
    tmp[k] = 0;

    char* p = tmp + 6;
    char* fname = p;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        *p = 0;
        vfs_write(fname, p + 1);
    } else {
        print_string("Usage: write <file> <content>\n");
    }
  }
    else if (str_compare(cmd, "clear") == 0) { clear_screen(); print_header(); }
    else if (str_compare(cmd, "info") == 0) {
        set_text_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        print_string("MysticOS v0.2.0\n32-bit protected mode\nPCI full bus scan\n");
    }
    else if (str_compare(cmd, "ver") == 0) {
        set_text_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        print_string("MysticOS 0.2.0-alpha (2025)\n");
    }
    else if (cmd[0]=='e' && cmd[1]=='c' && cmd[2]=='h' && cmd[3]=='o' && cmd[4]==' ') {
        set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        print_string(cmd+5); put_char('\n');
    }
    else if (str_compare(cmd, "mystic") == 0) {
        set_text_color(COLOR_MAGENTA, COLOR_BLACK);
        print_string(" #     #  #   #  ####  #####  ###  ###    ###    #### \n");
        print_string(" ##   ##   # #   #       *     #  #   #  #   #  #     \n");
        print_string(" # # # #    #    ####    #     #  #      #   #   ###  \n");
        print_string(" #  #  #    #        #   #     #  #   #  #   #      # \n");
        print_string(" #     #    #    ####    #    ###  ###    ###   ####  \n");
    }
    else if (str_compare(cmd, "devices") == 0) {
        set_text_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        print_string("Storage devices:\n");
        for (int i = 0; i < num_devices; i++) {
            if (devices[i].present) {
                set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
                print_string("  "); print_string(devices[i].name); print_string(" - ");
                switch (devices[i].type) {
                    case 0: print_string("ATA"); break;
                    case 1: print_string("NVMe"); break;
                    case 2: print_string("RAM disk"); break;
                }
                if (devices[i].size_sectors) {
                    print_string(", size: "); print_int(devices[i].size_sectors * 512 / 1024);
                    print_string(" KB");
                }
                put_char('\n');
            }
        }
    }
    else if (str_compare(cmd, "pci") == 0) {
        set_text_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        print_string("PCI devices (all buses):\n");
        for (int bus = 0; bus < 256; bus++) {
            for (int dev = 0; dev < 32; dev++) {
                for (int func = 0; func < 8; func++) {
                    unsigned short vendor = pci_get_vendor(bus, dev, func);
                    if (vendor == 0xFFFF) continue;
                    unsigned int dev_id = pci_read_config(bus, dev, func, 0);
                    unsigned int class_rev = pci_read_config(bus, dev, func, 8);
                    set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
                    print_string("  "); print_hex(bus); print_string(":");
                    print_hex(dev); print_string("."); print_hex(func);
                    print_string(" - "); print_hex(dev_id & 0xFFFF);
                    print_string(":"); print_hex((dev_id >> 16) & 0xFFFF);
                    print_string(" ["); print_hex(class_rev >> 24);
                    print_string(":"); print_hex((class_rev >> 16) & 0xFF);
                    print_string("]\n");
                }
            }
        }
    }
    else if (str_compare(cmd, "reboot") == 0) {
        print_string("Rebooting...\n");
        outb(0x64, 0xFE);
    }
    else if (str_compare(cmd, "ls") == 0) {
        vfs_ls("");
    }
    else if (cmd[0]=='l' && cmd[1]=='s' && cmd[2]==' ') {
        vfs_ls(cmd + 3);
    }
    else if (cmd[0]=='c' && cmd[1]=='d' && (cmd[2]==' ' || cmd[2]=='\0')) {
        if (cmd[2] == '\0') {
            vfs_cd("/");
        } else {
            vfs_cd(cmd + 3);
        }
    }
    else if (cmd[0]=='m' && cmd[1]=='k' && cmd[2]=='d' && cmd[3]=='i' && cmd[4]=='r' && cmd[5]==' ') {
        vfs_mkdir(cmd + 6);
    }
    else if (cmd[0]=='r' && cmd[1]=='m' && cmd[2]==' ') {
        vfs_rm(cmd + 3);
    }
    else if (str_compare(cmd, "mkfs") == 0) {
        set_text_color(COLOR_LIGHT_RED, COLOR_BLACK);
        print_string("WARNING: formatting current device as MSFS...\n");
        vfs_format();
    }
    else if (cmd[0]=='r' && cmd[1]=='u' && cmd[2]=='n' && cmd[3]==' ') {
        char fname[128];
        int k = 0;
        const char* p = cmd + 4;
        while (*p && k < 127) fname[k++] = *p++;
        fname[k] = 0;
        extern int vfs_read_file(const char* path, unsigned char* buf, unsigned int max);
        // Буфер для образа ELF — в ВЕРХНЕЙ RAM (4 МБ), а не в .bss,
        // чтобы не раздувать .bss и не лезть в мёртвую зону 0xA0000+.
        #define FILEBUF_BASE 0x00400000u
        #define FILEBUF_SIZE (512u * 1024u)
        unsigned char* filebuf = (unsigned char*)FILEBUF_BASE;
        int sz = vfs_read_file(fname, filebuf, FILEBUF_SIZE);
        if (sz <= 0) {
            set_text_color(COLOR_RED, COLOR_BLACK);
            print_string("run: cannot read file\n");
        } else {
            int rc = exec_run(filebuf, (unsigned int)sz, kernel_api());
            set_text_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
            print_string("[program exited with code "); print_int(rc); print_string("]\n");
        }
    }
    else if (cmd[0]) {
        set_text_color(COLOR_RED, COLOR_BLACK);
        print_string("Unknown: "); set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        print_string(cmd); print_string("\nType 'help'\n");
    }
}

// --------------------- Ядро ------------------------------
void kernel_main() {
    // IDT: теперь исключения выводят панику, а не роняют в ребут.
    idt_init();

    // PIC + PIT (100 Гц) + клавиатура (IRQ0/IRQ1).
    pic_init();
    pit_init(100);
    keyboard_init();

    set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    clear_screen();
    print_header();
    print_string("Init device manager... ");
    init_device_manager();
    print_string("OK\n");



    print_string("Detect filesystem... ");
    vfs_init();
    set_text_color(COLOR_GREEN, COLOR_BLACK);
    print_string(vfs_type_name());
    print_string("\n");
    set_text_color(COLOR_GREEN, COLOR_BLACK);
    print_string("OK\nWelcome to MysticOS!\nWrite 'help' for help\n");

    // Включаем аппаратные прерывания (таймер + клавиатура).
    interrupts_enable();

    char buf[256];
    while (1) { print_prompt(); read_string(buf, 256); process_command(buf); }
}

// ============================================================
// API v2: служебные обёртки для пользовательских ELF-программ
// ============================================================

// --- Экран ---
static void api_set_cursor(int x, int y) {
    if (x < 0) x = 0;
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
    cursor_x = x; cursor_y = y;
    update_cursor();
}
static int api_get_cursor_x(void) { return cursor_x; }
static int api_get_cursor_y(void) { return cursor_y; }
static int api_screen_width(void) { return SCREEN_WIDTH; }
static int api_screen_height(void) { return SCREEN_HEIGHT; }
static void api_scroll_up(void) {
    // Сдвигаем весь экран на строку вверх и чистим последнюю строку.
    for (int y = 1; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            video_buffer[(y-1)*SCREEN_WIDTH + x] = video_buffer[y*SCREEN_WIDTH + x];
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        video_buffer[(SCREEN_HEIGHT-1)*SCREEN_WIDTH + x].character = ' ';
        video_buffer[(SCREEN_HEIGHT-1)*SCREEN_WIDTH + x].color = current_color;
    }
    if (cursor_y > 0) cursor_y--;
    update_cursor();
}

// --- Ввод ---
static int api_key_available(void) {
    // Теперь ввод буферизуется в IRQ1, проверяем наш буфер.
    return keyboard_available() ? 1 : 0;
}
static int api_read_line(char* buf, int max) {
    read_string(buf, max);
    int n = 0; while (buf[n]) n++;
    return n;
}

// --- Строки ---
static int api_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int api_strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void api_strcpy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static void api_strcat(char* dst, const char* src, int max) {
    int i = 0; while (dst[i] && i < max - 1) i++;
    int j = 0;
    while (src[j] && i < max - 1) { dst[i++] = src[j++]; }
    dst[i] = 0;
}
static int api_atoi(const char* s) {
    int i = 0, neg = 0;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { neg = 1; i++; }
    int v = 0;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
    return neg ? -v : v;
}
static void api_itoa(int v, char* buf, int base) {
    if (base < 2) base = 10;
    char tmp[36]; int i = 0, neg = 0;
    unsigned int u = (unsigned int)v;
    if (v < 0 && base == 10) { neg = 1; u = (unsigned int)(-v); }
    if (u == 0) tmp[i++] = '0';
    while (u) { int d = u % base; tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= base; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}
static void api_memset(void* dst, int val, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    for (unsigned int i = 0; i < n; i++) d[i] = (unsigned char)val;
}
static void api_memcpy(void* dst, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
}
static char api_toupper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static char api_tolower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// --- ФС ---
static int api_chdir(const char* path) { return vfs_cd(path) ? MYSTIC_OK : MYSTIC_ENOENT; }
static void api_ls(const char* path) { vfs_ls(path); }
static void api_cat(const char* path) { vfs_cat(path); }
static int api_write_file(const char* path, const char* content) {
    vfs_write(path, content);
    return MYSTIC_OK;
}
static int api_mkdir(const char* path) { return vfs_mkdir(path) ? MYSTIC_OK : MYSTIC_EIO; }
static int api_rm(const char* path) { return vfs_rm(path) ? MYSTIC_OK : MYSTIC_EIO; }
static int api_read_file(const char* path, unsigned char* buf, unsigned int max) {
    return vfs_read_file(path, buf, max);
}
static int api_file_exists(const char* path) {
    unsigned char tmp[1];
    int r = vfs_read_file(path, tmp, 1);
    return (r >= 0) ? 1 : 0;
}
static int api_fs_type(void) { return (int)vfs_get_type(); }
static const char* api_fs_type_name(void) { return vfs_type_name(); }

// --- Память: кучa в ВЕРХНЕЙ RAM с аллокатором "first-fit + слияние" ---
// Куча живёт по фиксированному физическому адресу выше 1 МБ. GDT ядра
// flat (лимит 4 ГБ), поэтому адрес доступен напрямую. Так .bss остаётся
// крошечной и не залезает в мёртвую зону 0xA0000..0xFFFFF.
#define API_HEAP_BASE   0x00200000u      // 2 МБ
#define API_HEAP_SIZE   (4u * 1024u * 1024u)  // 4 МБ

// Заголовок каждого блока кучи. size включает сам заголовок.
typedef struct heap_block {
    unsigned int size;                // размер блока в байтах
    unsigned int free;                // 1 = свободен, 0 = занят
    struct heap_block* next;          // следующий блок в адресном порядке
    struct heap_block* prev;          // предыдущий блок
} heap_block_t;

static heap_block_t* g_heap_head = 0;
static unsigned int  g_heap_used = 0;   // занято (без заголовков свободных)

#define HEAP_ALIGN 8u
static unsigned int align8(unsigned int n) { return (n + (HEAP_ALIGN-1)) & ~(HEAP_ALIGN-1); }

static void heap_init(void) {
    unsigned char* base = (unsigned char*)API_HEAP_BASE;
    g_heap_head = (heap_block_t*)base;
    g_heap_head->size = API_HEAP_SIZE;
    g_heap_head->free = 1;
    g_heap_head->next = 0;
    g_heap_head->prev = 0;
    g_heap_used = 0;
}

static void* api_malloc(unsigned int size) {
    if (!g_heap_head) heap_init();
    if (size == 0) return 0;
    unsigned int need = align8(size) + sizeof(heap_block_t);

    for (heap_block_t* b = g_heap_head; b; b = b->next) {
        if (!b->free || b->size < need) continue;
        // Если остаток достаточно велик — отщепляем новый свободный блок.
        if (b->size >= need + sizeof(heap_block_t) + HEAP_ALIGN) {
            heap_block_t* nb = (heap_block_t*)((unsigned char*)b + need);
            nb->size = b->size - need;
            nb->free = 1;
            nb->next = b->next;
            nb->prev = b;
            if (b->next) b->next->prev = nb;
            b->next = nb;
            b->size = need;
        }
        b->free = 0;
        g_heap_used += b->size - sizeof(heap_block_t);
        return (void*)((unsigned char*)b + sizeof(heap_block_t));
    }
    return 0;   // нет места
}

static void api_free(void* ptr) {
    if (!ptr) return;
    heap_block_t* b = (heap_block_t*)((unsigned char*)ptr - sizeof(heap_block_t));
    if (b->free) return;   // двойное освобождение — игнор
    b->free = 1;
    if (b->size > sizeof(heap_block_t)) g_heap_used -= b->size - sizeof(heap_block_t);

    // Слияние со следующим блоком
    if (b->next && b->next->free) {
        b->size += b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    // Слияние с предыдущим
    if (b->prev && b->prev->free) {
        b->prev->size += b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

static void api_mem_info(mystic_meminfo_t* out) {
    if (!out) return;
    if (!g_heap_head) heap_init();
    out->heap_total = API_HEAP_SIZE;
    out->heap_used  = g_heap_used;
    out->heap_free  = API_HEAP_SIZE - g_heap_used;
}

// --- Система ---
// uptime — по тикам PIT (100 Гц => 10 мс на тик).
static unsigned int api_uptime_ms(void) {
    return timer_ticks() * 10u;
}

// sleep(ms) — активное ожидание по тикам PIT (hlt до истечения).
static void api_sleep(unsigned int ms) {
    unsigned int need = (ms + 9u) / 10u;   // тиков по 10 мс
    unsigned int start = timer_ticks();
    while ((timer_ticks() - start) < need) {
        __asm__ volatile ("hlt");
    }
}
static void api_reboot(void) { outb(0x64, 0xFE); }
static int api_devices_count(void) { return num_devices; }
static int api_device_info(int idx, mystic_device_t* out) {
    if (idx < 0 || idx >= num_devices || !out) return MYSTIC_EINVAL;
    for (int i = 0; i < 16; i++) out->name[i] = devices[idx].name[i];
    out->type = devices[idx].type;
    out->size_sectors = devices[idx].size_sectors;
    return MYSTIC_OK;
}
static void api_yield(void) {
    // Обновляем экран; время ведёт PIT (IRQ0).
    update_cursor();
}

// sys_exit: завершить программу, вернувшись в exec_run.
// В ring0 iret не восстанавливает esp, поэтому используем прямой
// ассемблерный прыжок (exec_do_exit в entry.asm).
extern void exec_do_exit(int code);
static void api_exit(int code) {
    exec_do_exit(code);   // не возвращается
}

// Таблица функций ядра, доступная ELF-программам (v2)
static mystic_api_t g_api = {
    MYSTIC_API_VERSION,
    // v1
    put_char,
    print_string,
    print_int,
    print_hex,
    clear_screen,
    set_text_color,
    get_char,
    vfs_getcwd,
    // v2: экран
    api_set_cursor,
    api_get_cursor_x,
    api_get_cursor_y,
    api_screen_width,
    api_screen_height,
    api_scroll_up,
    // v2: ввод
    api_key_available,
    api_read_line,
    // v2: строки
    api_strlen,
    api_strcmp,
    api_strcpy,
    api_strcat,
    api_atoi,
    api_itoa,
    api_memset,
    api_memcpy,
    api_toupper,
    api_tolower,
    // v2: ФС
    api_chdir,
    api_ls,
    api_cat,
    api_write_file,
    api_mkdir,
    api_rm,
    api_read_file,
    api_file_exists,
    api_fs_type,
    api_fs_type_name,
    // v2: память
    api_malloc,
    api_free,
    api_mem_info,
    // v2: система
    api_uptime_ms,
    api_reboot,
    api_devices_count,
    api_device_info,
    api_yield,
    // v3
    api_exit,
    api_sleep
};

const mystic_api_t* kernel_api(void) { return &g_api; }
