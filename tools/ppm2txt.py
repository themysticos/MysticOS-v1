#!/usr/bin/env python3
# ppm2txt.py - грубое распознавание текста VGA-скриншота (PPM P6).
# Использование: python3 ppm2txt.py screen.ppm [cols rows]
import sys

path = sys.argv[1]
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 80
rows = int(sys.argv[3]) if len(sys.argv) > 3 else 25

f = open(path, 'rb')
assert f.readline().strip() == b'P6'
dims = f.readline().split()
w, h = int(dims[0]), int(dims[1])
f.readline()
data = f.read()

for ry in range(rows):
    line = ''
    for rx in range(cols):
        x = int((rx + 0.5) * w / cols)
        y = int((ry + 0.5) * h / rows)
        i = (y * w + x) * 3
        b = (data[i] + data[i+1] + data[i+2]) // 3
        line += ' ' if b > 128 else ('.' if b > 40 else '#')
    print(line)
