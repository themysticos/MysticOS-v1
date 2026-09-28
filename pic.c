// pic.c - PIC 8259, PIT 8253/8254 и клавиатура (IRQ)
#include "pic.h"

void outb(unsigned short port, unsigned char value);
unsigned char inb(unsigned short port);
void print_string(const char* s);

// --- Порты ---
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define PIT_CH0   0x40
#define PIT_CMD   0x43

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STAT 0x64

// --- Заглушки IRQ (entry.asm) ---
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

// Регистрация IRQ-гейта в IDT (функция из idt.c).
void idt_set_gate_extern(int n, unsigned int handler);

static volatile unsigned int g_ticks = 0;

// Кольцевой буфер клавиатуры.
#define KB_BUF_SIZE 256
static volatile char kb_buf[KB_BUF_SIZE];
static volatile unsigned int kb_head = 0, kb_tail = 0;

static int shift_down = 0;
static int caps_lock = 0;

void pic_remap(void) {
    unsigned char a1 = inb(PIC1_DATA);
    unsigned char a2 = inb(PIC2_DATA);

    // ICW1: начало инициализации
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    // ICW2: смещение векторов (0x20 для master, 0x28 для slave)
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    // ICW3: связи master/slave
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    // ICW4: режим 8086
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // Маски: разрешаем IRQ0 (таймер) и IRQ1 (клавиатура), остальное запрещаем.
    outb(PIC1_DATA, 0xFC);   // 11111100: IRQ0,IRQ1 разрешены
    outb(PIC2_DATA, 0xFF);   // slave весь замаскирован

    (void)a1; (void)a2;
}

void pit_init(unsigned int hz) {
    unsigned int divisor = 1193182 / hz;
    if (divisor > 65535) divisor = 65535;
    outb(PIT_CMD, 0x36);                 // канал 0, режим 3 (square wave)
    outb(PIT_CH0, (unsigned char)(divisor & 0xFF));
    outb(PIT_CH0, (unsigned char)((divisor >> 8) & 0xFF));
}

static const char scancode_table[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0, 0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static char translate(unsigned char sc) {
    char base = scancode_table[sc & 0x7F];
    if (base >= 'a' && base <= 'z') {
        return (shift_down ^ caps_lock) ? (char)(base - 32) : base;
    }
    if (shift_down) {
        if (base >= '1' && base <= '9') return (char)(base - 16);
        if (base == '0') return ')';
        if (base == '-') return '_';
        if (base == '=') return '+';
        if (base == '[') return '{';
        if (base == ']') return '}';
        if (base == ';') return ':';
        if (base == '\'') return '"';
        if (base == ',') return '<';
        if (base == '.') return '>';
        if (base == '/') return '?';
        if (base == '`') return '~';
        if (base == '\\') return '|';
    }
    return base;
}

static void kb_push(char c) {
    unsigned int nh = (kb_head + 1) % KB_BUF_SIZE;
    if (nh != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = nh;
    }
}

void irq_handler(registers_t* r) {
    unsigned int irq = r->int_no - 0x20;

    if (irq == 0) {
        g_ticks++;
    } else if (irq == 1) {
        unsigned char sc = inb(KEYBOARD_DATA);
        if (sc == 0x2A || sc == 0x36) { shift_down = 1; }
        else if (sc == 0xAA || sc == 0xB6) { shift_down = 0; }
        else if (sc == 0x3A) { caps_lock = !caps_lock; }
        else if (!(sc & 0x80)) {
            char c = translate(sc);
            if (c) kb_push(c);
        }
    }

    // EOI
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void idt_set_gate_extern(int n, unsigned int handler);

void pic_init(void) {
    pic_remap();

    idt_set_gate_extern(0x20, (unsigned int)irq0);
    idt_set_gate_extern(0x21, (unsigned int)irq1);
    idt_set_gate_extern(0x22, (unsigned int)irq2);
    idt_set_gate_extern(0x23, (unsigned int)irq3);
    idt_set_gate_extern(0x24, (unsigned int)irq4);
    idt_set_gate_extern(0x25, (unsigned int)irq5);
    idt_set_gate_extern(0x26, (unsigned int)irq6);
    idt_set_gate_extern(0x27, (unsigned int)irq7);
    idt_set_gate_extern(0x28, (unsigned int)irq8);
    idt_set_gate_extern(0x29, (unsigned int)irq9);
    idt_set_gate_extern(0x2A, (unsigned int)irq10);
    idt_set_gate_extern(0x2B, (unsigned int)irq11);
    idt_set_gate_extern(0x2C, (unsigned int)irq12);
    idt_set_gate_extern(0x2D, (unsigned int)irq13);
    idt_set_gate_extern(0x2E, (unsigned int)irq14);
    idt_set_gate_extern(0x2F, (unsigned int)irq15);
}

void keyboard_init(void) {
    // Очищаем буфер контроллера, если там что-то завалялось.
    while (inb(KEYBOARD_STAT) & 1) inb(KEYBOARD_DATA);
}

void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

unsigned int timer_ticks(void) { return g_ticks; }

int keyboard_available(void) {
    return kb_head != kb_tail;
}

int keyboard_getchar(void) {
    while (kb_head == kb_tail) { __asm__ volatile ("hlt"); }
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

void keyboard_flush(void) {
    kb_tail = kb_head;
}
