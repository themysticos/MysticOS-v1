[ORG 0x7C00]
[BITS 16]

start:
    jmp short boot_code
    nop


; Гарантируем, что код начнется строго на 62-м байте (смещение 0x3E)
times 62 - ($ - $$) db 0

boot_code:
    ; 1. Сброс сегментных регистров
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; 2. Загружаем ядро по адресу 0x1000:0x0000 (линейный 0x10000).
    ;    Читаем по ОДНОМУ сектору в цикле с корректным переходом
    ;    через границы дорожек/головок/цилиндров: одиночный вызов
    ;    int 13h с al>1 не умеет пересекать границу дорожки (63 сектора),
    ;    из-за чего ядро >61 сектора читалось битым.
    mov word [kern_sectors], 120    ; сколько секторов ядра максимум
    mov byte [chs_sector], 2        ; начинаем со 2-го сектора (LBA 1)
    mov byte [chs_head], 0
    mov byte [chs_cyl], 0
    mov bx, 0x1000
    mov es, bx
    xor bx, bx                      ; ES:BX = 0x1000:0x0000 (линейный 0x10000)

read_loop:
    cmp word [kern_sectors], 0
    je read_done

    mov ah, 0x02            ; функция чтения секторов
    mov al, 1               ; ровно один сектор
    mov ch, [chs_cyl]
    mov cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; ES:BX += 512
    add bx, 512
    jnc .no_wrap
    mov ax, es
    add ax, 0x20            ; +512 байт к сегменту (0x20 * 16 = 512)
    mov es, ax
.no_wrap:
    dec word [kern_sectors]

    ; --- продвинуть CHS на следующий сектор ---
    inc byte [chs_sector]
    cmp byte [chs_sector], 64    ; сектора идут 1..63
    jb read_loop
    mov byte [chs_sector], 1     ; новая дорожка
    inc byte [chs_head]
    cmp byte [chs_head], 32      ; головки 0..31 (по BPB)
    jb read_loop
    mov byte [chs_head], 0       ; новый цилиндр
    inc byte [chs_cyl]
    jmp read_loop

read_done:

    ; 3. Включение линии A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 4. Переход в 32-битный защищенный режим
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 5. Дальний прыжок прямо в точку входа ядра (entry.asm -> _start)
    jmp dword 0x08:0x10000

disk_error:
    mov si, msg_error
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
.next_char:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next_char
.done:
    ret

boot_drive db 0
kern_sectors dw 0
chs_sector   db 2
chs_head     db 0
chs_cyl      db 0
msg_error  db 'Disk Error!', 13, 10, 0

; Таблица GDT
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF ; Код селектор (0x08)
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF ; Данные селектор (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
