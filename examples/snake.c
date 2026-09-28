// snake.c - игра "Змейка" для MysticOS (API v3).
// Использует: key_available/sleep (таймер PIT), set_cursor, put_char.
// Сборка: sh build.sh snake.c snake.elf

#include "../api.h"

#define W 40
#define H 20
#define MAXLEN (W * H)

static int snake_x[MAXLEN], snake_y[MAXLEN];
static int sn_len;
static int dir_x, dir_y;
static int food_x, food_y;
static unsigned int rng_state;

static unsigned int rnd(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}

static void place_food(void) {
    // Еду ставим только во ВНУТРЕННЕЙ области (1..W-2, 1..H-2),
    // чтобы она не появлялась вплотную к стенам — иначе съесть её
    // можно только врезавшись в стену.
    for (;;) {
        int fx = 1 + (int)(rnd() % (W - 2));
        int fy = 1 + (int)(rnd() % (H - 2));
        int hit = 0;
        for (int i = 0; i < sn_len; i++)
            if (snake_x[i] == fx && snake_y[i] == fy) { hit = 1; break; }
        if (!hit) { food_x = fx; food_y = fy; return; }
    }
}

static void draw_border(const mystic_api_t* api) {
    api->set_color(8, 0);
    api->set_cursor(0, 0);
    for (int x = 0; x < W + 2; x++) api->put_char('#');
    for (int y = 0; y < H; y++) {
        api->set_cursor(0, y + 1); api->put_char('#');
        api->set_cursor(W + 1, y + 1); api->put_char('#');
    }
    api->set_cursor(0, H + 1);
    for (int x = 0; x < W + 2; x++) api->put_char('#');
}

static void draw_cell(const mystic_api_t* api, int x, int y, char c, unsigned char col) {
    api->set_color(col, 0);
    api->set_cursor(x + 1, y + 1);
    api->put_char(c);
}

static void draw_game(const mystic_api_t* api, int old_tail_x, int old_tail_y, int ate) {
    // Стираем старый хвост (если не выросли на этом шаге).
    if (!ate && old_tail_x >= 0)
        draw_cell(api, old_tail_x, old_tail_y, ' ', 0);
    // Еда
    draw_cell(api, food_x, food_y, '*', 12);
    // Змейка: голова
    draw_cell(api, snake_x[0], snake_y[0], 'O', 10);
    // Тело
    for (int i = 1; i < sn_len; i++)
        draw_cell(api, snake_x[i], snake_y[i], 'o', 2);
}

int _start(const mystic_api_t* api) {
    api->clear_screen();
    api->set_color(11, 0);
    api->print_string("  SNAKE — arrows/WASD to move, 'q' to quit\n\n");

    sn_len = 3;
    snake_x[0] = W / 2;     snake_y[0] = H / 2;
    snake_x[1] = W / 2 - 1; snake_y[1] = H / 2;
    snake_x[2] = W / 2 - 2; snake_y[2] = H / 2;
    dir_x = 1; dir_y = 0;
    rng_state = 0x12345678u;
    place_food();

    draw_border(api);
    draw_game(api, -1, -1, 0);   // первый кадр: хвост не стираем

    int alive = 1;
    while (alive) {
        // --- Ввод (неблокирующий) ---
        while (api->key_available()) {
            char c = api->get_char();
            if (c == 'q' || c == 'Q' || c == 27) { alive = 0; break; }
            if ((c == 'w' || c == 'W') && dir_y == 0) { dir_x = 0; dir_y = -1; }
            else if ((c == 's' || c == 'S') && dir_y == 0) { dir_x = 0; dir_y = 1; }
            else if ((c == 'a' || c == 'A') && dir_x == 0) { dir_x = -1; dir_y = 0; }
            else if ((c == 'd' || c == 'D') && dir_x == 0) { dir_x = 1; dir_y = 0; }
        }
        if (!alive) break;

        // --- Шаг ---
        int nx = snake_x[0] + dir_x;
        int ny = snake_y[0] + dir_y;

        // Стены
        if (nx < 0 || nx >= W || ny < 0 || ny >= H) { alive = 0; break; }
        // Хвост (кроме последнего — он уйдёт)
        for (int i = 0; i < sn_len - 1; i++)
            if (snake_x[i] == nx && snake_y[i] == ny) { alive = 0; break; }
        if (!alive) break;

        int ate = (nx == food_x && ny == food_y);

        // Запоминаем старый хвост (последний сегмент) — его затрём,
        // если змейка не выросла.
        int old_tail_x = snake_x[sn_len - 1];
        int old_tail_y = snake_y[sn_len - 1];

        if (ate && sn_len < MAXLEN) sn_len++;

        // Сдвиг тела
        for (int i = sn_len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i - 1];
            snake_y[i] = snake_y[i - 1];
        }
        snake_x[0] = nx; snake_y[0] = ny;

        if (ate) place_food();
        draw_game(api, old_tail_x, old_tail_y, ate);

        // Скорость: 140 мс на шаг
        api->sleep(140);
    }

    api->set_color(12, 0);
    api->set_cursor(0, H + 3);
    api->print_string("GAME OVER! Score: ");
    api->print_int(sn_len - 3);
    api->print_string("\n");
    api->set_color(8, 0);
    api->print_string("Press any key...");
    api->get_char();
    api->clear_screen();
    return sn_len - 3;
}
