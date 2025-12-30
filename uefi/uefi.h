#ifndef _UEFI_H_
#define _UEFI_H_

#include <stdint.h>

// 基本データ型
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void VOID;
typedef uint16_t CHAR16;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef VOID *EFI_HANDLE;

#ifndef NULL
#define NULL ((VOID *)0)
#endif

///
/// 64-bit physical memory address.
///
typedef UINT64 EFI_PHYSICAL_ADDRESS;

///
/// 64-bit virtual memory address.
///
typedef UINT64 EFI_VIRTUAL_ADDRESS;

#define EFI_SUCCESS 0

// プロトコル定義
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

struct FileBuffer
{
    VOID *buffer;
    UINT64 size;
};
struct BootVolumeConfig
{
    struct FileBuffer kernel_file;
    struct FileBuffer bootloader_file;
};

// ELFヘッダー (ファイルの先頭にある管理情報)
typedef struct
{
    unsigned char e_ident[16]; // マジックナンバー (0x7F 'E' 'L' 'F' ...)
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry; // 【重要】カーネルのエントリーポイントアドレス
    Elf64_Off e_phoff;  // プログラムヘッダーテーブルへのオフセット
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum; // プログラムヘッダーの数
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

// プログラムヘッダー (メモリへの展開方法が書いてある)
typedef struct
{
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;   // ファイル内のデータの位置
    Elf64_Addr p_vaddr;   // メモリ上の展開先アドレス (Virtual)
    Elf64_Addr p_paddr;   // メモリ上の展開先アドレス (Physical)
    Elf64_Xword p_filesz; // ファイル内のサイズ
    Elf64_Xword p_memsz;  // メモリ上で確保すべきサイズ
    Elf64_Xword p_align;
} Elf64_Phdr;

#define PT_LOAD 1 // ロードすべきセグメント

#ifndef EFI_TIME_DEFINED
#define EFI_TIME_DEFINED
typedef struct
{
    UINT16 Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    UINT32 Nanosecond;
    UINT16 TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
} EFI_TIME;
#endif

// 関数ポインタの呼び出し規約をMS ABIに強制する（重要）
#if defined(__x86_64__)
#define EFIAPI __attribute__((ms_abi))
#elif defined(__aarch64__)
#define EFIAPI
#else
#define EFIAPI
#endif

typedef enum
{
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory, // OSが自由に使っていいメモリ
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum
{
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

// メモリ記述子 (GetMemoryMapで取得される1要素)
typedef struct
{
    UINT32 Type; // EFI_MEMORY_TYPE
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages; // 1 Page = 4KB
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

// GetMemoryMap 関数の型定義
typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *MemoryMapSize,
                                               EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                               UINTN *MapKey,
                                               UINTN *DescriptorSize,
                                               UINT32 *DescriptorVersion);

// テキスト出力関数
typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);

// GUID (Global Unique Identifier)
typedef struct
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    uint8_t Data4[8];
} EFI_GUID;

// GOPの前方宣言
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

// ピクセルフォーマット定義
typedef enum
{
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct
{
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct
{
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

// 画面モード情報 (解像度やフレームバッファのアドレス)
typedef struct
{
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

// GOP本体の定義
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    VOID *QueryMode;
    VOID *SetMode;
    VOID *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode; // ここから現在のモード情報を取得
};

// LocateProtocol 関数の型定義
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol,
                                                VOID *Registration,
                                                VOID **Interface);

// AllocatePool / FreePool の型定義
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType,
                                              UINTN Size, VOID **Buffer);

typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(VOID *Buffer);

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type,
                                               EFI_MEMORY_TYPE MemoryType,
                                               UINTN Pages,
                                               EFI_PHYSICAL_ADDRESS *Memory);

typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS Memory,
                                           UINTN Pages);

#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                         \
    {0x5B1B31A1,                                                               \
     0x9562,                                                                   \
     0x11D2,                                                                   \
     {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                   \
    {0x0964E5B22,                                                              \
     0x6459,                                                                   \
     0x11D2,                                                                   \
     {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_FILE_INFO_ID                                                       \
    {0x09576E92,                                                               \
     0x6D3F,                                                                   \
     0x11D2,                                                                   \
     {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

// プロトコルの前方宣言
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;

// --- Loaded Image Protocol (自分自身のデバイスハンドルを知るために必要) ---
struct _EFI_LOADED_IMAGE_PROTOCOL
{
    UINT32 Revision;
    VOID *ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    VOID *DeviceHandle; // これが欲しい (起動ディスクのハンドル)
    VOID *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    EFI_ALLOCATE_POOL Unload;
};

// --- File System Protocol ---

// ファイルを開くモード
#define EFI_FILE_MODE_READ 0x0000000000000001
#define EFI_FILE_READ_ONLY 0x0000000000000001

// ファイル属性 (GetInfo用)
typedef struct
{
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime; // EFI_TIMEは未定義ですがパディングとして無視
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[];
} EFI_FILE_INFO;

// ファイル操作関数ポインタ定義
typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *This,
                                          EFI_FILE_PROTOCOL **NewHandle,
                                          CHAR16 *FileName, UINT64 OpenMode,
                                          UINT64 Attributes);

typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *This);

typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *This,
                                          UINTN *BufferSize, VOID *Buffer);

typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *This,
                                                  UINT64 Position);

typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *This,
                                              EFI_GUID *InformationType,
                                              UINTN *BufferSize, VOID *Buffer);

// EFI_FILE_PROTOCOL 本体
struct _EFI_FILE_PROTOCOL
{
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    VOID *Delete;
    EFI_FILE_READ Read;
    VOID *Write;
    VOID *GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    VOID *SetInfo;
    VOID *Flush;
};

// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL 本体
typedef EFI_STATUS(EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    UINT64 Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};

// HandleProtocol の型定義
typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(VOID *Handle,
                                                EFI_GUID *Protocol,
                                                VOID **Interface);

typedef struct
{
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
    UINT64 EcamBaseAddress; // AArch64 ECAM用
    UINT8 EcamStartBus;
    UINT8 EcamEndBus;
    UINT8 EcamPadding[6]; // アライメント用
} FrameBufferConfig;

struct MemoryMap
{
    UINTN buffer_size;
    VOID *buffer;
    UINTN map_size;
    UINTN map_key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
};

// メモリ操作関数 (標準ライブラリを使わないため自作)
// static inline にすることでヘッダーだけで完結させます
static inline VOID CopyMem(VOID *Dest, VOID *Src, UINTN Size)
{
    uint8_t *d = (uint8_t *)Dest;
    uint8_t *s = (uint8_t *)Src;
    for (UINTN i = 0; i < Size; i++)
        d[i] = s[i];
}

static inline VOID SetMem(VOID *Dest, UINTN Size, uint8_t Value)
{
    uint8_t *d = (uint8_t *)Dest;
    for (UINTN i = 0; i < Size; i++)
        d[i] = Value;
}

// ExitBootServices の型定義 (Index 29)
typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle,
                                                   UINTN MapKey);

// BootServices (LocateProtocolを使うために拡張)
typedef struct
{
    char _header[24];
    char _pad_reserved_1[16];
    EFI_ALLOCATE_PAGES AllocatePages; // Index 2
    EFI_FREE_PAGES FreePages;         // Index 3
    EFI_GET_MEMORY_MAP GetMemoryMap;  // Index 4
    EFI_ALLOCATE_POOL AllocatePool;   // Index 5
    EFI_FREE_POOL FreePool;           // Index 6
    char _pad_before_handle_protocol[72];
    EFI_HANDLE_PROTOCOL HandleProtocol; // Index 16

    // --- [修正] Index 17-28 をスキップ (12関数 * 8 = 96 byte) ---
    char _pad_before_exit_boot_services[96];

    EFI_EXIT_BOOT_SERVICES ExitBootServices; // Index 29 [追加]

    // --- [修正] Index 30-36 をスキップ (7関数 * 8 = 56 byte) ---
    char _pad_after_exit_boot_services[56];

    EFI_LOCATE_PROTOCOL LocateProtocol; // Index 37
} EFI_BOOT_SERVICES;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    VOID *Reset;
    EFI_TEXT_STRING OutputString;
    VOID *TestString;
    VOID *QueryMode;
    VOID *SetMode;
    VOID *SetAttribute;
    VOID *ClearScreen;
    VOID *SetCursorPosition;
    VOID *EnableCursor;
    VOID *Mode;
};

// EFI Configuration Table Entry
typedef struct
{
    EFI_GUID VendorGuid;
    VOID *VendorTable;
} EFI_CONFIGURATION_TABLE;

struct _EFI_SYSTEM_TABLE
{
    char _header[24];

    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    char _padding[4]; // アライメント調整 (8バイト境界に合わせるため)
    // -------------------------

    VOID *ConsoleInHandle;
    VOID *ConIn;
    VOID *ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    VOID *StandardErrorHandle;
    VOID *StdErr;
    VOID *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
};

// =================================================================
// ACPI構造体 (ECAM用)
// =================================================================

// ACPI 2.0+ GUID
#define ACPI_20_TABLE_GUID                                                     \
    {0x8868E871,                                                               \
     0xE4F1,                                                                   \
     0x11D3,                                                                   \
     {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}}

// ACPI RSDP (Root System Description Pointer)
typedef struct
{
    char Signature[8]; // "RSD PTR "
    UINT8 Checksum;
    char OEMID[6];
    UINT8 Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress; // ACPI 2.0+
    UINT8 ExtendedChecksum;
    UINT8 Reserved[3];
} __attribute__((packed)) ACPI_RSDP;

// ACPI Table Header
typedef struct
{
    char Signature[4];
    UINT32 Length;
    UINT8 Revision;
    UINT8 Checksum;
    char OEMID[6];
    char OEMTableID[8];
    UINT32 OEMRevision;
    UINT32 CreatorID;
    UINT32 CreatorRevision;
} __attribute__((packed)) ACPI_TABLE_HEADER;

// MCFG Entry (PCI ECAM領域情報)
typedef struct
{
    UINT64 BaseAddress; // ECAMベースアドレス
    UINT16 SegmentGroup;
    UINT8 StartBus;
    UINT8 EndBus;
    UINT32 Reserved;
} __attribute__((packed)) MCFG_ENTRY;

#endif // _UEFI_H_