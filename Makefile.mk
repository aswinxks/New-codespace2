# ============================================================
# MAKEFILE - Build ALL-IN-ONE UEFI BOOTKIT
# ============================================================

SRC = permanent_bootkit.c
TARGET = bootx86.efi

all:
	@echo "Building ALL-IN-ONE UEFI Bootkit..."
	@echo "[*] amsix86.sys embedded in bootx86.efi"
	gcc -I/usr/include/efi -I/usr/include/efi/x86_64 \
	    -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
	    -Wall -O2 -D__GNU_EFI__ -DMDE_CPU_X64 \
	    -c $(SRC) -o $(SRC:.c=.o)
	ld -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
	   -L/usr/lib -lgnuefi -lefi -nostdlib -znocombreloc \
	   $(SRC:.c=.o) -o $(TARGET).so
	objcopy -O binary $(TARGET).so $(TARGET)
	@echo ""
	@echo "[+] $(TARGET) built!"
	@echo "    Features:"
	@echo "    - F9 = HD-Player.exe (NO BEEP)"
	@echo "    - F10 = Notepad.exe (BEEP!)"
	@echo "    - F8+F11 = Open Notepad.exe"
	@echo "    - .mui in UEFI memory (NO DISK!)"
	@echo "    - amsix86.sys embedded"
	@echo "    - Auto-inject via .sys"

clean:
	rm -f *.o *.so $(TARGET)