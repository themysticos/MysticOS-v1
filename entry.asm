; entry.asm
[BITS 32]

section .text
global _start
extern kernel_main

_start:
    mov ax, 0x10         ; селектор данных
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000     ; стек: выше .bss (она теперь ~0x28000)
                         ; и ниже видео-памяти 0xA0000

    call kernel_main

    cli
    hlt
    jmp $

; int exec_call(int (*entry)(void*), void* arg, unsigned int stack_top)
; Переключает стек на stack_top, вызывает entry(arg), возвращает
; результат и восстанавливает стек ядра. Аргументы — cdecl:
;   [esp+4]=entry  [esp+8]=arg  [esp+12]=stack_top
global exec_call
; Глобалы для sys_exit: куда вернуться, если программа вызовет exit().
global g_exec_ret_esp
global g_exec_ret_eip
section .bss
g_exec_ret_esp: resd 1
g_exec_ret_eip: resd 1
section .text

exec_call:
    ; cdecl: [esp+4]=entry [esp+8]=arg [esp+12]=stack_top
    ; Сохраняем стек ядра и адрес возврата ДО каких-либо push.
    mov [g_exec_ret_esp], esp    ; esp на входе (указывает на адрес возврата)
    mov esi, [esp]               ; адрес возврата в exec_run
    mov [g_exec_ret_eip], esi

    mov eax, [esp+4]         ; entry
    mov ecx, [esp+8]         ; arg
    mov edx, [esp+12]        ; stack_top

    mov ebx, esp             ; сохранить стек ядра в ebx

    mov esp, edx             ; переключиться на стек программы
    push ecx                 ; аргумент entry(arg)
    call eax                 ; entry(arg) -> eax
    mov esp, ebx             ; вернуть стек ядра

    ret

; void exec_do_exit(int code) — завершает программу, возвращаясь в exec_run.
; В ring0 iret не восстанавливает esp, поэтому прыгаем напрямую:
; восстанавливаем стек ядра и jmp на адрес возврата exec_call. Не возвращается.
global exec_do_exit
exec_do_exit:
    mov eax, [esp+4]              ; code выхода
    mov esp, [g_exec_ret_esp]     ; стек ядра: [esp] = адрес возврата
    ret                           ; вернуться в exec_run (eax = код)

; ============================================================
;  IDT: загрузка таблицы и заглушки исключений (векторы 0-31)
; ============================================================
global idt_load
idt_load:
    mov eax, [esp+4]      ; адрес idt_ptr
    lidt [eax]
    ret

; Общий обработчик: сохраняет регистры, зовёт C isr_handler, возвращается.
extern isr_handler
global isr_common
isr_common:
    pusha                 ; edi,esi,ebp,esp,ebx,edx,ecx,eax
    mov ax, ds
    push eax              ; сохранить ds

    mov ax, 0x10          ; сегмент данных ядра
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp              ; аргумент: указатель на registers_t
    call isr_handler
    add esp, 4

    pop eax               ; восстановить ds
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8            ; убрать int_no и err_code
    iret

; --- Сисколл int 0x80 ---
extern syscall_handler
global isr128
isr128:
    push 0                ; фиктивный код ошибки
    push 0x80             ; номер вектора
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call syscall_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

; Заглушки. %1 — номер вектора. Если у вектора НЕТ кода ошибки от CPU,
; пушим фиктивный 0, чтобы структура registers_t была одинаковой.
%macro ISR_NOERR 1
    global isr%1
    isr%1:
        push 0            ; фиктивный код ошибки
        push %1           ; номер вектора
        jmp isr_common
%endmacro

%macro ISR_ERR 1
    global isr%1
    isr%1:
        ; CPU уже положил код ошибки
        push %1           ; номер вектора
        jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ============================================================
;  IRQ: аппаратные прерывания (векторы 0x20-0x2F)
; ============================================================
global irq_common
extern irq_handler
irq_common:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8            ; int_no + err_code
    iret

%macro IRQ 2
    global irq%1
    irq%1:
        push 0            ; фиктивный код ошибки
        push %2           ; номер вектора (0x20+)
        jmp irq_common
%endmacro

IRQ 0,  0x20
IRQ 1,  0x21
IRQ 2,  0x22
IRQ 3,  0x23
IRQ 4,  0x24
IRQ 5,  0x25
IRQ 6,  0x26
IRQ 7,  0x27
IRQ 8,  0x28
IRQ 9,  0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F