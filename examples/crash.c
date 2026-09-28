// crash.c - намеренно вызывает исключение, чтобы проверить IDT/панику.
// Демонстрирует, что вместо молчаливого ребута ядро печатает KERNEL PANIC
// с вектором, EIP и регистрами.
// Сборка: sh build.sh crash.c crash.elf

#include "../api.h"

static int read_int(const mystic_api_t* api) {
    char buf[16];
    int i = 0;
    for (;;) {
        char c = api->get_char();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') { if (i > 0) { i--; api->put_char('\b'); } }
        else if (c >= '0' && c <= '9') { if (i < 15) { buf[i++] = c; api->put_char(c); } }
    }
    buf[i] = 0;
    api->put_char('\n');
    int v = 0, k = 0;
    for (; buf[k]; k++) v = v * 10 + (buf[k] - '0');
    return v;
}

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(12, 0);
    api->print_string("  ==============================\n");
    api->print_string("      MysticOS CRASH TEST\n");
    api->print_string("  ==============================\n\n");

    api->set_color(7, 0);
    api->print_string("  This program will trigger a CPU exception\n");
    api->print_string("  on purpose, to test the panic handler.\n\n");
    api->print_string("  Choose an exception:\n");
    api->print_string("    1) Divide by zero   (vector 0)\n");
    api->print_string("    2) Invalid opcode   (vector 6)\n");
    api->print_string("    3) Breakpoint       (vector 3, int3)\n");
    api->print_string("    4) Overflow         (vector 4, into)\n");
    api->print_string("    0) Cancel\n\n");

    api->set_color(14, 0);
    api->print_string("  choice: ");
    api->set_color(15, 0);
    int choice = read_int(api);

    api->set_color(11, 0);
    api->print_string("\n  Triggering...\n");

    if (choice == 1) {
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;   // деление на ноль -> vector 0
        (void)c;
    } else if (choice == 2) {
        __asm__ volatile ("ud2");  // недействительный опкод -> vector 6
    } else if (choice == 3) {
        __asm__ volatile ("int3"); // точка останова -> vector 3
    } else if (choice == 4) {
        __asm__ volatile ("into"); // переполнение -> vector 4
    } else {
        api->set_color(10, 0);
        api->print_string("  Cancelled. Returning to shell.\n");
        return 0;
    }

    api->set_color(10, 0);
    api->print_string("  No exception? That's unexpected.\n");
    return 0;
}
