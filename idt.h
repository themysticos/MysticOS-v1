// idt.h - таблица прерываний и обработка исключений MysticOS
#ifndef IDT_H
#define IDT_H

// Инициализация IDT и регистрация обработчиков исключений (векторы 0-31).
void idt_init(void);

// Запись IDT (8 байт).
typedef struct {
    unsigned short base_low;    // младшие 16 бит адреса обработчика
    unsigned short selector;    // селектор кода (0x08)
    unsigned char  always0;     // всегда 0
    unsigned char  flags;       // 0x8E = present, ring0, 32-bit interrupt gate
    unsigned short base_high;   // старшие 16 бит адреса
} __attribute__((packed)) idt_entry_t;

// Указатель для инструкции lidt.
typedef struct {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed)) idt_ptr_t;

// Регистры, которые сохраняют наши ISR-заглушки.
typedef struct {
    unsigned int ds;                                        // сегмент данных
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;    // pusha
    unsigned int int_no, err_code;                          // номер вектора, код ошибки
    unsigned int eip, cs, eflags, useresp, ss;              // пушит CPU
} __attribute__((packed)) registers_t;

// Загрузка IDT (entry.asm).
extern void idt_load(unsigned int idt_ptr_addr);

// Регистрирует шлюз сисколла int 0x80 (entry.asm: isr128).
extern void isr128(void);

#endif
