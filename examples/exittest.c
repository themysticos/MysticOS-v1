// exittest.c - проверка сисколла exit (int 0x80).
// Программа завершается из СЕРЕДИНЫ через api->exit, а не через return.
// Сборка: sh build.sh exittest.c exittest.elf

#include "../api.h"

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(11, 0);
    api->print_string("  ==============================\n");
    api->print_string("     MysticOS sys_exit test\n");
    api->print_string("  ==============================\n\n");

    api->set_color(7, 0);
    api->print_string("  This line prints before exit.\n");
    api->print_string("  Calling api->exit(42) now...\n");

    api->exit(42);

    // Сюда управление НЕ должно вернуться:
    api->set_color(12, 0);
    api->print_string("  ERROR: exit did not work!\n");
    return 1;
}
