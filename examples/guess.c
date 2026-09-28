// guess.c - игра "угадай число" для MysticOS.
// Демонстрирует get_char, циклы, сравнения, простой LCG-ГПСЧ.
// Сборка: sh build.sh guess.c guess.elf

#include "../api.h"

static unsigned int g_seed;
static unsigned int rnd(void) {
    g_seed = g_seed * 1103515245u + 12345u;
    return (g_seed >> 16) & 0x7FFF;
}

static int read_int(const mystic_api_t* api) {
    char buf[16];
    int i = 0;
    for (;;) {
        char c = api->get_char();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') { if (i > 0) { i--; api->put_char('\b'); } }
        else if (c >= '0' && c <= '9') { if (i < 15) { buf[i++] = c; api->put_char(c); } }
        else if (c == '-') { if (i == 0) { buf[i++] = c; api->put_char(c); } }
    }
    buf[i] = 0;
    api->put_char('\n');
    int v = 0, neg = 0, k = 0;
    if (buf[0] == '-') { neg = 1; k = 1; }
    for (; buf[k]; k++) v = v * 10 + (buf[k] - '0');
    return neg ? -v : v;
}

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(13, 0);
    api->print_string("  ==============================\n");
    api->print_string("   Guess the number (1..100)!\n");
    api->print_string("  ==============================\n\n");

    api->set_color(8, 0);
    api->print_string("  Type any number to seed the RNG: ");
    api->set_color(15, 0);
    int seed = read_int(api);
    g_seed = (unsigned int)seed ^ 0x9E3779B9u;
    rnd(); rnd();

    int target = (int)(rnd() % 100) + 1;
    int tries = 0;

    for (;;) {
        api->set_color(14, 0);
        api->print_string("  Your guess: ");
        api->set_color(15, 0);
        int g = read_int(api);
        tries++;

        if (g < target) {
            api->set_color(11, 0);
            api->print_string("  Too LOW.\n");
        } else if (g > target) {
            api->set_color(12, 0);
            api->print_string("  Too HIGH.\n");
        } else {
            api->set_color(10, 0);
            api->print_string("  CORRECT! The number was ");
            api->print_int(target);
            api->print_string(".\n  Attempts: ");
            api->print_int(tries);
            api->print_string("\n");
            break;
        }
    }

    api->set_color(8, 0);
    api->print_string("\n  [Press any key to exit]");
    api->get_char();
    api->clear_screen();
    return tries;
}
