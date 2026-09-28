#!/bin/sh
# build.sh - сборка примера ELF-программы для MysticOS
# Требуется gcc с -m32 (gcc-multilib).
set -e

SRC="${1:-hello.c}"
OUT="${2:-hello.elf}"

# -nostdlib: без стандартной библиотеки
# -static -no-pie: фиксированные адреса, без релокаций
# -Wl,-Ttext=0x1000000: грузим на 16 МБ (выше порога exec_run,
#   чтобы программа не могла перезаписать ядро)
# -Wl,-e,_start: точка входа _start
gcc -m32 -ffreestanding -nostdlib -static -no-pie -fno-pic \
    -Wl,-Ttext-segment=0x1000000 -Wl,-e,_start \
    -Wl,--build-id=none \
    "$SRC" -o "$OUT"

echo "Built $OUT"
readelf -h "$OUT" | head -20
