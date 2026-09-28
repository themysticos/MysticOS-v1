// hello.c - тестовая ELF-программа для MysticOS (использует API ядра)
// Сборка: см. build.sh

#include "../api.h"

int _start(const mystic_api_t* api) {
    api->set_color(14, 0);  // жёлтый
    api->print_string("================================\n");
    api->set_color(11, 0);  // голубой
    api->print_string("  Hello from an ELF program!\n");
    api->set_color(10, 0);  // зелёный
    api->print_string("  Using the MysticOS kernel API.\n");
    api->set_color(14, 0);
    api->print_string("================================\n");
    api->set_color(7, 0);

    // Покажем текущий каталог и спросим имя
    api->print_string("cwd: ");
    api->print_string(api->getcwd());
    api->print_string("\nPress any key...");
    api->get_char();
    api->print_string("\n");

    return 42;
}
