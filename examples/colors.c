// colors.c - демонстрация всех 16 цветов VGA через API ядра MysticOS
// Сборка: sh build.sh colors.c colors.elf

#include "../api.h"

static const char* color_names[16] = {
    "Black",        "Blue",         "Green",     "Cyan",
    "Red",          "Magenta",      "Brown",     "Light Gray",
    "Dark Gray",    "Light Blue",   "Light Green","Light Cyan",
    "Light Red",    "Light Magenta","Yellow",    "White"
};

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(15, 0);
    api->print_string("=== MysticOS VGA Color Table ===\n\n");

    for (int i = 0; i < 16; i++) {
        api->set_color(7, 0);
        api->print_string("  ");
        api->print_int(i);
        api->print_string("  ");

        api->set_color((unsigned char)i, 0);
        api->print_string("##########  ");

        api->set_color(15, 0);
        api->print_string(color_names[i]);
        api->print_string("\n");
    }

    api->set_color(8, 0);
    api->print_string("\n[Press any key to exit]");
    api->get_char();
    api->clear_screen();
    return 0;
}
