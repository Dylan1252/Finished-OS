# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage
TEST_BIN      = $(OUTPUT_FOLDER)/ext2-host-test
QEMU          = qemu-system-i386
QEMU_MEMORY   = 128

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386


.PHONY: all build clean kernel iso run debug disk user-shell inserter user-app user-hello user-clock insert-shell insert-test-fixture test test-ext2

run: all
	@$(QEMU) -m $(QEMU_MEMORY) -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

# Start paused with the GDB stub enabled. Use `make run` for normal boot.
debug: all
	@$(QEMU) -m $(QEMU_MEMORY) -s -S -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
disk:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M
all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel
	rm -rf $(OUTPUT_FOLDER)/shell $(OUTPUT_FOLDER)/inserter $(TEST_BIN) $(TEST_BIN).exe
	rm -rf $(OUTPUT_FOLDER)/iso/


kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@rm -f $(OUTPUT_FOLDER)/*.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/text/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process/process.c -o $(OUTPUT_FOLDER)/process.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/process/scheduler.c -o $(OUTPUT_FOLDER)/scheduler.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/process/context-switch.s -o $(OUTPUT_FOLDER)/context-switch.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/cmos.c -o $(OUTPUT_FOLDER)/cmos.o
	@$(LIN) $(LFLAGS) bin/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f $(OUTPUT_FOLDER)/*.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/


# Build the user-space shell: crt0 + shell + string helpers -> flat binary
user-shell:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o $(OUTPUT_FOLDER)/crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o $(OUTPUT_FOLDER)/user-shell.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/user-string.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		$(OUTPUT_FOLDER)/crt0.o $(OUTPUT_FOLDER)/user-shell.o $(OUTPUT_FOLDER)/user-string.o \
		-o $(OUTPUT_FOLDER)/shell
	@rm -f $(OUTPUT_FOLDER)/crt0.o $(OUTPUT_FOLDER)/user-shell.o $(OUTPUT_FOLDER)/user-string.o

# Host-side tool to insert the shell into the EXT2 disk image
inserter:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) -w -fno-builtin -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		$(SOURCE_FOLDER)/filesystem/ext2.c \
		-o $(OUTPUT_FOLDER)/inserter

# Build an external user application: crt0 + app + string helpers -> flat binary
# usage: make user-app APP=hello
user-app:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o $(OUTPUT_FOLDER)/crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/external/$(APP).c -o $(OUTPUT_FOLDER)/$(APP).o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/user-string.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		$(OUTPUT_FOLDER)/crt0.o $(OUTPUT_FOLDER)/$(APP).o $(OUTPUT_FOLDER)/user-string.o \
		-o $(OUTPUT_FOLDER)/$(APP)
	@rm -f $(OUTPUT_FOLDER)/crt0.o $(OUTPUT_FOLDER)/$(APP).o $(OUTPUT_FOLDER)/user-string.o

user-hello:
	@$(MAKE) --no-print-directory user-app APP=hello

user-clock:
	@$(MAKE) --no-print-directory user-app APP=clock

insert-shell: inserter user-shell user-hello user-clock
	@cd $(OUTPUT_FOLDER) && ./inserter -f $(DISK_NAME).bin 1 shell
	@cd $(OUTPUT_FOLDER) && ./inserter -f $(DISK_NAME).bin 1 hello
	@cd $(OUTPUT_FOLDER) && ./inserter -f $(DISK_NAME).bin 1 clock

# Optional fixture used by the shell's file-based grep/copy/move tests.
insert-test-fixture: inserter
	@cd $(OUTPUT_FOLDER) && ./inserter -f $(DISK_NAME).bin 1 ../tests/fixtures/grep-fixture.txt

# Host-side EXT2 tests use an in-memory disk image and do not require QEMU.
test: test-ext2

test-ext2:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) -std=gnu11 -Wall -Wextra -Werror -fno-builtin -I$(SOURCE_FOLDER) \
		tests/ext2_host_test.c $(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/stdlib/string.c -o $(TEST_BIN)
	@./$(TEST_BIN)
