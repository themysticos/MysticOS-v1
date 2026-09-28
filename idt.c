// idt.c - таблица прерываний и обработчик исключений MysticOS
#include "idt.h"

void print_string(const char* s);
void put_char(char c);
void print_int(int v);
void print_hex(unsigned int v);
void set_text_color(unsigned char fg, unsigned char bg);

#define COLOR_WHITE     15
#define COLOR_RED        4
#define COLOR_LIGHT_RED 12
#define COLOR_YELLOW    14
#define COLOR_LIGHT_GREY 7
#define COLOR_BLACK      0

static idt_entry_t g_idt[256];
static idt_ptr_t   g_idt_ptr;

// Ассемблерные заглушки (entry.asm).
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

static void idt_set_gate(int n, unsigned int handler) {
    g_idt[n].base_low  = handler & 0xFFFF;
    g_idt[n].base_high = (handler >> 16) & 0xFFFF;
    g_idt[n].selector  = 0x08;
    g_idt[n].always0   = 0;
    g_idt[n].flags     = 0x8E;
}

// Публичная обёртка для регистрации IRQ-гейтов (используется pic.c).
void idt_set_gate_extern(int n, unsigned int handler) {
    idt_set_gate(n, handler);
}

static const char* exc_name(unsigned int n) {
    switch (n) {
        case 0:  return "Divide-by-zero";
        case 1:  return "Debug";
        case 2:  return "Non-maskable interrupt";
        case 3:  return "Breakpoint";
        case 4:  return "Overflow";
        case 5:  return "Bound range exceeded";
        case 6:  return "Invalid opcode";
        case 7:  return "Device not available";
        case 8:  return "Double fault";
        case 9:  return "Coprocessor segment overrun";
        case 10: return "Invalid TSS";
        case 11: return "Segment not present";
        case 12: return "Stack-segment fault";
        case 13: return "General protection fault";
        case 14: return "Page fault";
        case 16: return "x87 floating-point";
        case 17: return "Alignment check";
        case 18: return "Machine check";
        case 19: return "SIMD floating-point";
        default: return "Reserved/unknown";
    }
}

// Общий C-обработчик, вызывается из ассемблерных заглушек.
void isr_handler(registers_t* r) {
    set_text_color(COLOR_WHITE, COLOR_BLACK);
    print_string("\n");
    set_text_color(COLOR_LIGHT_RED, COLOR_BLACK);
    print_string("=== KERNEL PANIC ===\n");
    set_text_color(COLOR_YELLOW, COLOR_BLACK);
    print_string("Exception: ");
    set_text_color(COLOR_WHITE, COLOR_BLACK);
    print_string(exc_name(r->int_no));
    print_string(" (vector ");
    print_int((int)r->int_no);
    print_string(")\n");

    set_text_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    print_string("  err code : "); print_hex(r->err_code); put_char('\n');
    print_string("  EIP      : "); print_hex(r->eip);      put_char('\n');
    print_string("  CS       : "); print_hex(r->cs);       put_char('\n');
    print_string("  EFLAGS   : "); print_hex(r->eflags);   put_char('\n');
    print_string("  EAX      : "); print_hex(r->eax);      put_char('\n');
    print_string("  EBX      : "); print_hex(r->ebx);      put_char('\n');
    print_string("  ESP      : "); print_hex(r->esp);      put_char('\n');
    print_string("\nSystem halted. Reboot to continue.\n");

    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void idt_init(void) {
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (unsigned int)&g_idt;

    unsigned char* p = (unsigned char*)g_idt;
    for (unsigned int i = 0; i < sizeof(g_idt); i++) p[i] = 0;

    idt_set_gate(0,  (unsigned int)isr0);
    idt_set_gate(1,  (unsigned int)isr1);
    idt_set_gate(2,  (unsigned int)isr2);
    idt_set_gate(3,  (unsigned int)isr3);
    idt_set_gate(4,  (unsigned int)isr4);
    idt_set_gate(5,  (unsigned int)isr5);
    idt_set_gate(6,  (unsigned int)isr6);
    idt_set_gate(7,  (unsigned int)isr7);
    idt_set_gate(8,  (unsigned int)isr8);
    idt_set_gate(9,  (unsigned int)isr9);
    idt_set_gate(10, (unsigned int)isr10);
    idt_set_gate(11, (unsigned int)isr11);
    idt_set_gate(12, (unsigned int)isr12);
    idt_set_gate(13, (unsigned int)isr13);
    idt_set_gate(14, (unsigned int)isr14);
    idt_set_gate(15, (unsigned int)isr15);
    idt_set_gate(16, (unsigned int)isr16);
    idt_set_gate(17, (unsigned int)isr17);
    idt_set_gate(18, (unsigned int)isr18);
    idt_set_gate(19, (unsigned int)isr19);
    idt_set_gate(20, (unsigned int)isr20);
    idt_set_gate(21, (unsigned int)isr21);
    idt_set_gate(22, (unsigned int)isr22);
    idt_set_gate(23, (unsigned int)isr23);
    idt_set_gate(24, (unsigned int)isr24);
    idt_set_gate(25, (unsigned int)isr25);
    idt_set_gate(26, (unsigned int)isr26);
    idt_set_gate(27, (unsigned int)isr27);
    idt_set_gate(28, (unsigned int)isr28);
    idt_set_gate(29, (unsigned int)isr29);
    idt_set_gate(30, (unsigned int)isr30);
    idt_set_gate(31, (unsigned int)isr31);

    // Сисколл int 0x80.
    idt_set_gate(0x80, (unsigned int)isr128);

    idt_load((unsigned int)&g_idt_ptr);
}

// ============================================================
//  Сисколлы (int 0x80)
// ============================================================
// Программа кладёт номер сисколла в eax, аргументы — в ebx, ecx, edx.
// Возврат — в eax (обрабатывается в ассемблере, если нужно).

// Номера сисколлов (общие с user-программами, см. api.h).
#define SYS_EXIT     1
#define SYS_PUTCHAR  2
#define SYS_PRINT    3

// Адрес возврата в exec_run (entry.asm), для sys_exit.
extern unsigned int g_exec_ret_esp;
extern unsigned int g_exec_ret_eip;

void syscall_handler(registers_t* r) {
    switch (r->eax) {
        case SYS_EXIT:
            // Завершение программы: подменяем iret-кадр так, чтобы вернуться
            // в exec_run (как будто entry() вернулась), с кодом в eax.
            r->eip = g_exec_ret_eip;
            r->esp = g_exec_ret_esp + 4;   // пропустить сохранённый ebx
            r->eax = r->ebx;               // код выхода
            break;
        case SYS_PUTCHAR:
            put_char((char)r->ebx);
            break;
        case SYS_PRINT:
            print_string((const char*)r->ebx);
            break;
        default:
            print_string("[unknown syscall ");
            print_int((int)r->eax);
            print_string("]\n");
            break;
    }
}
