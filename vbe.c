// vbe.c - драйвер линейного framebuffer (VBE, 24bpp) + bitmap-шрифт.
#include "vbe.h"

// Параметры VBE, сохранённые загрузчиком по адресу 0x9000:
//   0x9000: dword fb_addr | 0x9004: word pitch | 0x9006: w | 0x9008: h
#define VBE_PARAM_ADDR 0x9000u

static unsigned int g_fb = 0;
static unsigned int g_pitch = 0;
static int g_w = 0, g_h = 0;
static int g_active = 0;

unsigned int vbe_fb_addr(void) { return g_fb; }
unsigned int vbe_width(void) { return (unsigned int)g_w; }
unsigned int vbe_height(void) { return (unsigned int)g_h; }
unsigned int vbe_pitch(void) { return g_pitch; }
int vbe_active(void) { return g_active; }

int vbe_init(void) {
    volatile unsigned int* p = (volatile unsigned int*)VBE_PARAM_ADDR;
    unsigned int fb = p[0];
    if (fb == 0) { g_active = 0; return 0; }
    g_fb = fb;
    g_pitch = (unsigned int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 4));
    g_w = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 6));
    g_h = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 8));
    g_active = 1;
    // ДИАГНОСТИКА: залить экран тёмно-синим, чтобы проверить запись в LFB.
    fb_clear(0x000080);
    return 1;
}

// Пиксель (24bpp: 3 байта B,G,R).
void fb_put_pixel(int x, int y, unsigned int color) {
    if (!g_active) return;
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    unsigned char* p = (unsigned char*)(g_fb + (unsigned int)y * g_pitch + (unsigned int)x * 3u);
    p[0] = (unsigned char)(color & 0xFF);          // B
    p[1] = (unsigned char)((color >> 8) & 0xFF);   // G
    p[2] = (unsigned char)((color >> 16) & 0xFF);  // R
}

void fb_fill_rect(int x, int y, int w, int h, unsigned int color) {
    if (!g_active) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            fb_put_pixel(xx, yy, color);
}

void fb_clear(unsigned int color) {
    if (!g_active) return;
    unsigned char b = (unsigned char)(color & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char r = (unsigned char)((color >> 16) & 0xFF);
    for (int y = 0; y < g_h; y++) {
        unsigned char* row = (unsigned char*)(g_fb + (unsigned int)y * g_pitch);
        for (int x = 0; x < g_w; x++) {
            row[x*3+0] = b;
            row[x*3+1] = g;
            row[x*3+2] = r;
        }
    }
}

// --- Текст через шрифт BIOS (8x8, символы 0..127) ---
// В real mode BIOS держит шрифт по 0xF000:0xFA6E => линейный 0xFFA6E.
// Каждый символ: 8 байт, по одному биту на пиксель (слева направо).
#define FONT8_ADDR 0xFFA6Eu

void fb_draw_char(int cx, int cy, char ch, unsigned int fg, unsigned int bg) {
    if (!g_active) return;
    unsigned char c = (unsigned char)ch;
    const unsigned char* glyph = (const unsigned char*)(FONT8_ADDR + (unsigned int)c * 8u);
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int on = (bits & (0x80 >> col)) ? 1 : 0;
            fb_put_pixel(cx + col, cy + row, on ? fg : bg);
        }
    }
}

