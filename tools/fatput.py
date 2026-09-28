#!/usr/bin/env python3
# fatput.py - положить файл в FAT16-образ MysticOS без mtools.
# Использование: python3 fatput.py <image> <local_file> <DEST.NAME>
# Понимает геометрию из BPB (в т.ч. нестандартную -s 64).

import sys, struct

SECTOR = 512

def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]

def make_83(name):
    # 'HELLO.ELF' -> b'HELLO   ELF'
    name = name.upper()
    if '.' in name:
        base, ext = name.split('.', 1)
    else:
        base, ext = name, ''
    base = (base[:8]).ljust(8)
    ext = (ext[:3]).ljust(3)
    return (base + ext).encode('ascii')

def main():
    if len(sys.argv) != 4:
        print("usage: fatput.py <image> <local_file> <DEST.NAME>")
        return 1
    img_path, local_path, dest_name = sys.argv[1:4]

    with open(img_path, 'r+b') as f:
        img = bytearray(f.read())

        bps       = u16(img, 11)
        spc       = img[13]
        reserved  = u16(img, 14)
        nfats     = img[16]
        root_ents = u16(img, 17)
        spf       = u16(img, 22)   # sectors per FAT (FAT16)
        if bps != 512:
            print("error: only 512-byte sectors supported")
            return 1

        fat_start   = reserved
        root_start  = reserved + nfats * spf
        root_sectors= (root_ents * 32 + SECTOR - 1) // SECTOR
        data_start  = root_start + root_sectors

        print(f"geometry: spc={spc} reserved={reserved} nfats={nfats} spf={spf} root_ents={root_ents}")
        print(f"fat@{fat_start} root@{root_start} data@{data_start}")

        # читаем файл
        with open(local_path, 'rb') as lf:
            filedata = lf.read()
        cluster_size = spc * SECTOR
        nclusters = (len(filedata) + cluster_size - 1) // cluster_size
        print(f"file size={len(filedata)} needs {nclusters} clusters")

        # найдём свободные кластеры, читая FAT
        def fat_get(cl):
            off = fat_start * SECTOR + cl * 2
            return u16(img, off)
        def fat_set(cl, val):
            off = fat_start * SECTOR + cl * 2
            struct.pack_into('<H', img, off, val)

        free = []
        max_cl = (len(img) - data_start * SECTOR) // cluster_size + 2
        for cl in range(2, max_cl):
            if fat_get(cl) == 0:
                free.append(cl)
                if len(free) == nclusters:
                    break
        if len(free) < nclusters:
            print("error: not enough free clusters")
            return 1

        # помечаем цепочку в FAT (все копии FAT)
        for i, cl in enumerate(free):
            nxt = free[i+1] if i+1 < len(free) else 0xFFFF
            for k in range(nfats):
                base = (reserved + k * spf) * SECTOR
                struct.pack_into('<H', img, base + cl * 2, nxt)

        # пишем данные
        for i, cl in enumerate(free):
            chunk = filedata[i*cluster_size:(i+1)*cluster_size]
            chunk = chunk + b'\x00' * (cluster_size - len(chunk))
            off = data_start * SECTOR + (cl - 2) * cluster_size
            img[off:off+cluster_size] = chunk

        # имя 8.3 для записи
        name83 = make_83(dest_name)

        # сначала ищем существующую запись с таким же именем —
        # чтобы перезаписать её, а не создать дубликат.
        entry = None
        free_slot = None
        for e in range(root_ents):
            off = root_start * SECTOR + e * 32
            if img[off] == 0x00:
                if free_slot is None:
                    free_slot = off
                break
            if img[off] == 0xE5:
                if free_slot is None:
                    free_slot = off
                continue
            if img[off:off+11] == name83:
                entry = off
                print(f'note: {dest_name} already exists -> overwriting')
                break
        if entry is None:
            entry = free_slot
        if entry is None:
            print("error: root directory full")
            return 1

        rec = bytearray(32)
        rec[0:11] = name83
        rec[11] = 0x00          # attr: обычный файл
        struct.pack_into('<H', rec, 26, free[0])   # first cluster low
        struct.pack_into('<I', rec, 28, len(filedata))  # size
        img[entry:entry+32] = rec

        f.seek(0)
        f.write(img)
        print(f"OK: {dest_name} -> clusters {free[:8]}{'...' if len(free)>8 else ''}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
