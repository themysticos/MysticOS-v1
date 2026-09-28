// sysinfo.c - информационная панель MysticOS (API v2).
// Демонстрирует: getcwd, fs_type_name, mem_info, devices, uptime,
// print_int/print_hex, set_cursor, progress-бары.
// Сборка: sh build.sh sysinfo.c sysinfo.elf

#include "../api.h"

static void labeled_bar(const mystic_api_t* api, const char* label,
                        int percent, unsigned char color) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int width = 28;
    int filled = width * percent / 100;

    api->set_color(7, 0);
    api->print_string("  ");
    api->print_string(label);
    int ll = api->strlen(label);
    for (int i = ll; i < 12; i++) api->put_char(' ');

    api->set_color(8, 0);
    api->put_char('[');
    api->set_color(color, 0);
    for (int i = 0; i < filled; i++) api->put_char('#');
    api->set_color(8, 0);
    for (int i = filled; i < width; i++) api->put_char('.');
    api->put_char(']');

    api->set_color(15, 0);
    api->put_char(' ');
    api->print_int(percent);
    api->print_string("%\n");
}

int _start(const mystic_api_t* api) {
    api->clear_screen();

    api->set_color(11, 0);
    api->print_string("  +----------------------------------------+\n");
    api->print_string("  |       M y s t i c O S   S t a t s       |\n");
    api->print_string("  +----------------------------------------+\n\n");

    // Версия API
    api->set_color(14, 0);
    api->print_string("  API version : ");
    api->set_color(15, 0);
    api->print_int((int)api->version);
    api->print_string("\n");

    // Тип ФС и рабочий каталог
    api->set_color(14, 0);
    api->print_string("  Filesystem  : ");
    api->set_color(10, 0);
    api->print_string(api->fs_type_name());
    api->print_string("\n");

    api->set_color(14, 0);
    api->print_string("  Working dir : ");
    api->set_color(10, 0);
    api->print_string(api->getcwd());
    api->print_string("\n\n");

    // Память кучи
    mystic_meminfo_t mem;
    api->mem_info(&mem);
    int used_pct = (int)(mem.heap_used * 100u / (mem.heap_total ? mem.heap_total : 1u));
    api->set_color(13, 0);
    api->print_string("  Heap:\n");
    labeled_bar(api, "used", used_pct, 12);
    api->set_color(7, 0);
    api->print_string("    total ");
    api->print_int((int)(mem.heap_total / 1024));
    api->print_string(" KB, free ");
    api->print_int((int)(mem.heap_free / 1024));
    api->print_string(" KB\n\n");

    // Устройства
    api->set_color(13, 0);
    api->print_string("  Block devices:\n");
    int n = api->devices_count();
    for (int i = 0; i < n; i++) {
        mystic_device_t dev;
        if (api->device_info(i, &dev) != MYSTIC_OK) continue;
        api->set_color(7, 0);
        api->print_string("    ");
        api->print_string(dev.name);
        int nl = api->strlen(dev.name);
        for (int k = nl; k < 10; k++) api->put_char(' ');
        const char* t = (dev.type == 0) ? "ATA" : (dev.type == 1) ? "NVMe" : "RAM";
        api->set_color(11, 0);
        api->print_string(t);
        if (dev.size_sectors) {
            api->set_color(7, 0);
            api->print_string("  ");
            api->print_int((int)(dev.size_sectors * 512u / 1024u));
            api->print_string(" KB");
        }
        api->put_char('\n');
    }

    // Uptime
    api->set_color(14, 0);
    api->print_string("\n  Uptime      : ");
    api->set_color(15, 0);
    api->print_int((int)api->uptime_ms());
    api->print_string(" ms\n");

    api->set_color(8, 0);
    api->print_string("\n  [Press any key to exit]");
    api->get_char();
    api->clear_screen();
    return 0;
}
