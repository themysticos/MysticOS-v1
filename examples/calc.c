// calc.c - интерактивный калькулятор MysticOS.
// Использует get_char для ввода и put_char для эха.
// Сборка: sh build.sh calc.c calc.elf

#include "../api.h"

// Читает строку до Enter. Возвращает длину. Строка в buf.
static int read_line(const mystic_api_t* api, char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = api->get_char();
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (i > 0) { i--; api->put_char('\b'); }
        } else if (c >= ' ') {
            buf[i++] = c;
            api->put_char(c);
        }
    }
    buf[i] = 0;
    api->put_char('\n');
    return i;
}

// Парсит целое (поддерживает минус). Возвращает 1 при успехе.
static int parse_int(const char* s, int* out) {
    int i = 0, neg = 0;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { neg = 1; i++; }
    if (s[i] < '0' || s[i] > '9') return 0;
    int v = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    *out = neg ? -v : v;
    return 1;
}

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(11, 0);
    api->print_string("  MysticOS Calculator\n");
    api->set_color(8, 0);
    api->print_string("  Operands: + - * / %   (empty line quits)\n\n");

    char a_buf[32], op_buf[8], b_buf[32];
    for (;;) {
        api->set_color(14, 0);
        api->print_string("  a = ");
        api->set_color(15, 0);
        if (read_line(api, a_buf, sizeof(a_buf)) == 0) break;

        api->set_color(14, 0);
        api->print_string("  op= ");
        api->set_color(15, 0);
        if (read_line(api, op_buf, sizeof(op_buf)) == 0) break;

        api->set_color(14, 0);
        api->print_string("  b = ");
        api->set_color(15, 0);
        if (read_line(api, b_buf, sizeof(b_buf)) == 0) break;

        int a, b;
        if (!parse_int(a_buf, &a) || !parse_int(b_buf, &b)) {
            api->set_color(12, 0);
            api->print_string("  error: bad number\n\n");
            continue;
        }

        char op = op_buf[0];
        int r = 0, ok = 1;
        if (op == '+') r = a + b;
        else if (op == '-') r = a - b;
        else if (op == '*') r = a * b;
        else if (op == '/') { if (b == 0) ok = 0; else r = a / b; }
        else if (op == '%') { if (b == 0) ok = 0; else r = a % b; }
        else ok = 0;

        api->set_color(10, 0);
        api->print_string("  = ");
        if (ok) {
            api->print_int(r);
            api->print_string("\n\n");
        } else {
            api->set_color(12, 0);
            api->print_string("? (division by zero or bad operator)\n\n");
        }
    }

    api->set_color(8, 0);
    api->print_string("  Bye.\n");
    return 0;
}
