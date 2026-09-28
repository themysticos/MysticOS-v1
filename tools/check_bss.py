#!/usr/bin/env python3
# check_bss.py - проверяет, что .bss ядра не залезает в мёртвую зону
# 0xA0000..0xFFFFF (видео RAM + BIOS ROM), куда запись не держится.
# Использование: python3 check_bss.py kernel.elf
import subprocess
import sys

DANGER = 0xA0000

path = sys.argv[1] if len(sys.argv) > 1 else 'kernel.elf'

try:
    out = subprocess.check_output(['readelf', '-S', '-W', path], text=True)
except Exception as e:
    print(f'check_bss: cannot read {path}: {e}')
    sys.exit(1)

for line in out.splitlines():
    line = line.strip()
    if line.startswith('[') and '.bss' in line:
        # формат readelf -W -S:
        # [ N] .bss NOBITS addr off size ...
        parts = line.split()
        # parts: ['[', '8]', '.bss', 'NOBITS', '00019460', '00a460', '002144', ...]
        try:
            i = parts.index('.bss')
            # формат: .bss NOBITS <addr> <offset> <size> ...
            addr = int(parts[i + 2], 16)
            size = int(parts[i + 4], 16)
        except (ValueError, IndexError):
            print(f'check_bss: cannot parse line: {line}')
            sys.exit(1)
        end = addr + size
        print(f'check_bss: .bss {addr:#x}..{end:#x} '
              f'({size} bytes)')
        if end >= DANGER:
            print(f'check_bss: ERROR: .bss ends at {end:#x}, '
                  f'which is >= {DANGER:#x} (video/BIOS hole).')
            print('check_bss: move large buffers out of .bss '
                  '(use upper RAM, e.g. 0x200000+).')
            print('BSS_FAIL')
            sys.exit(1)
        print('check_bss: OK')
        print('BSS_OK')
        sys.exit(0)

print('check_bss: no .bss section found')
sys.exit(0)
