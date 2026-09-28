// vbe.h - VESA BIOS Extensions: переключение в графический режим
#ifndef VBE_H
#define VBE_H

// Пытается включить графический режим 640x480x32 через VBE.
// Возвращает 1 при успехе (framebuffer доступен), 0 - остаёмся в тексте.
int vbe_init(void);

// Доступ к framebuffer.
unsigned int vbe_fb_addr(void);   // физический адрес LFB
unsigned int vbe_width(void);
unsigned int vbe_height(void);
unsigned int vbe_pitch(void);     // байт на строку
int vbe_active(void);             // 1, если графический режим включён

// Примитивы рисования (работают только если vbe_active()).
void fb_put_pixel(int x, int y, unsigned int color);
void fb_fill_rect(int x, int y, int w, int h, unsigned int color);
void fb_clear(unsigned int color);
void fb_draw_char(int cx, int cy, char ch, unsigned int fg, unsigned int bg);

// Цвет в формате 0x00RRGGBB.
#define FB_RGB(r, g, b) (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

#endif
