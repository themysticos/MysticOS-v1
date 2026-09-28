// box.c - ASCII-графика: рамки, узоры и цветные полосы
// Демонстрирует put_char/set_color для посимвольной отрисовки.
// Сборка: sh build.sh box.c box.elf

#include "../api.h"

static void draw_hline(const mystic_api_t* api, char c, int n) {
    for (int i = 0; i < n; i++) api->put_char(c);
}

static void draw_box(const mystic_api_t* api, int w, int h, unsigned char color) {
    api->set_color(color, 0);
    api->put_char('+'); draw_hline(api, '-', w - 2); api->put_char('+');
    api->put_char('\n');
    for (int y = 0; y < h - 2; y++) {
        api->put_char('|');
        for (int x = 0; x < w - 2; x++) api->put_char(' ');
        api->put_char('|');
        api->put_char('\n');
    }
    api->put_char('+'); draw_hline(api, '-', w - 2); api->put_char('+');
    api->put_char('\n');
}

int _start(const mystic_api_t* api) {
    api->clear_screen();

    draw_box(api, 40, 3, 11);

    for (int c = 1; c < 16; c++) {
        api->set_color((unsigned char)c, 0);
        api->print_string("  ");
        draw_hline(api, '#', 30);
        api->put_char('\n');
    }

    api->set_color(14, 0);
    api->print_string("\n");
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 30; x++) {
            if (x == y || x == 29 - y) api->put_char('*');
            else api->put_char(' ');
        }
        api->put_char('\n');
    }

    draw_box(api, 40, 3, 13);

    api->set_color(8, 0);
    api->print_string("\n[Press any key to exit]");
    api->get_char();
    api->clear_screen();
    return 0;
}
