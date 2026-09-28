// pic.h - PIC, PIT и обработка IRQ для MysticOS
#ifndef PIC_H
#define PIC_H

#include "idt.h"

// Инициализация PIC, PIT и регистрация IRQ-обработчиков.
void pic_init(void);
void pit_init(unsigned int hz);
void keyboard_init(void);

// Включает аппаратные прерывания (sti).
void interrupts_enable(void);

// Общий C-обработчик IRQ (вызывается из entry.asm).
void irq_handler(registers_t* r);

// Тики таймера (инкремент в IRQ0).
unsigned int timer_ticks(void);

// Неблокирующий ввод: 1, если есть символ; -1, если нет.
int keyboard_available(void);
// Читает символ из буфера (блокирующе, ждёт).
int keyboard_getchar(void);
// Очищает буфер клавиатуры (сбрасывает застрявшие символы).
void keyboard_flush(void);

#endif
