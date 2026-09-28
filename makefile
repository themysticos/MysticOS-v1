NASM    = nasm
GCC     = gcc
LD      = ld
OBJCOPY = objcopy

CFLAGS  = -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pie -fno-common -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

BOOTLOADER = bootloader.bin
KERNEL_ELF = kernel.elf
KERNEL_BIN = kernel.bin
IMAGE      = mysticos.img

all: $(IMAGE)

$(BOOTLOADER): bootloader.asm
	$(NASM) -f bin $< -o $@

entry.o: entry.asm
	$(NASM) -f elf32 $< -o $@

kernel.o: kernel.c
	$(GCC) $(CFLAGS) -c $< -o $@

fat.o: fat.c
	$(GCC) $(CFLAGS) -c $< -o $@

vfs.o: vfs.c
	$(GCC) $(CFLAGS) -c $< -o $@

msfs.o: msfs.c
	$(GCC) $(CFLAGS) -c $< -o $@

exec.o: exec.c
	$(GCC) $(CFLAGS) -c $< -o $@

idt.o: idt.c
	$(GCC) $(CFLAGS) -c $< -o $@

pic.o: pic.c
	$(GCC) $(CFLAGS) -c $< -o $@

vbe.o: vbe.c
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): entry.o kernel.o fat.o vfs.o msfs.o exec.o idt.o pic.o vbe.o
	$(LD) $(LDFLAGS) -o $@ $^
	@# Проверка: ядро грузится на 0x10000, .bss НЕ должна заходить
	@# в мёртвую зону 0xA0000..0xFFFFF (видео/BIOS), иначе глобалы
	@# "не держат" запись. Считаем адрес конца .bss и падаем, если >=0xA0000.
	@python3 tools/check_bss.py $@ | grep -q 'BSS_OK' || (echo '*** BSS CHECK FAILED ***'; false)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMAGE): $(BOOTLOADER) $(KERNEL_BIN)
	# 0. Удаляем старый образ: mkfs.fat -C отказывается перезаписывать
	#    существующий файл ("already exists"), из-за чего повторный
	#    `make` падал. Чистим перед созданием.
	rm -f $@
	# 1. Создаем образ FAT16 размером ровно 2 ГБ (4194304 блоков по 512 байт)
	#    Флаг -s 64 принудительно выставляет размер кластера в 64 сектора (32 КБ)
	#    Флаг -R 64 сохраняет наши зарезервированные сектора под ядро
	mkfs.fat -C $@ -F 16 -s 64 -R 64 524288

	# 2. ХАК-ВЖИВЛЕНИЕ: Вырезаем валидную BPB-таблицу огромного диска и вживляем в загрузчик
	dd if=$@ of=$(BOOTLOADER) bs=1 count=59 skip=3 seek=3 conv=notrunc

	# 3. Записываем твой обновленный загрузчик обратно в 0 сектор
	dd if=$(BOOTLOADER) of=$@ conv=notrunc

	# 4. Записываем ядро (kernel.bin) в свободные сектора сразу за загрузчиком (seek=1)
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc

	# 5. test.txt кладётся отдельной целью testfile (mtools капризничает
	#    с media-type образа, созданного mkfs.fat -s 64).

testfile: $(IMAGE)
	cd examples && sh build.sh
	python3 tools/fatput.py $(IMAGE) examples/hello.elf HELLO.ELF

disk2.img:
	python3 -c "open('disk2.img','wb').write(b'\x00' * (64*1024*1024))"
	echo "Created raw disk2.img (64 MB)"

clean:
	rm -f *.o *.elf *.bin mysticos.img test.txt

run: $(IMAGE) disk2.img
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE) \
		-drive format=raw,file=disk2.img -m 256

.PHONY: all clean run
