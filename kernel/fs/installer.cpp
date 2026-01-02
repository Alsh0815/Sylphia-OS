#include "fs/installer.hpp"
#include "cxx.hpp"
#include "driver/nvme/nvme_driver.hpp"
#include "driver/usb/mass_storage/mass_storage.hpp"
#include "driver/usb/xhci.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "fs/gpt.hpp"
#include "memory/memory_manager.hpp"
#include "printk.hpp"

namespace FileSystem
{

// EFI System Partition (ESP) のGUID
// C12A7328-F81F-11D2-BA4B-00A0C93EC93B
const GUID kEspGuid = {0xC12A7328,
                       0xF81F,
                       0x11D2,
                       {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};

// ランダムなGUID (今回は固定値で代用)
const GUID kUniqueGuid = {0x12345678,
                          0xABCD,
                          0xEFEF,
                          {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}};

// プロトタイプ宣言 (gpt.cppで実装したCRC32関数)
uint32_t CalculateCRC32(const void *buffer, size_t length);

void FormatDiskGPT(uint64_t total_blocks)
{
    if (!NVMe::g_nvme)
        return;

    kprintf("[Installer] Formatting Disk with GPT (Fixed PRP Issue)...\n");

    // バッファ確保 (4KBアライメント必須)
    uint8_t *sector_buf =
        static_cast<uint8_t *>(MemoryManager::Allocate(512, 4096));

    // ---------------------------------------------------------
    // 1. Protective MBR (LBA 0)
    // ---------------------------------------------------------
    memset(sector_buf, 0, 512);
    LegacyMBR *mbr = reinterpret_cast<LegacyMBR *>(sector_buf);
    mbr->partitions[0].boot_indicator = 0x00;
    mbr->partitions[0].sys_type = 0xEE;
    mbr->partitions[0].start_lba = 1;
    mbr->partitions[0].size_lba =
        (total_blocks > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)(total_blocks - 1);
    mbr->signature = 0xAA55;
    NVMe::g_nvme->Write(0, sector_buf, 1);

    // ---------------------------------------------------------
    // 2. Partition Entries (データ作成)
    // ---------------------------------------------------------
    size_t entry_cnt = 128;
    size_t entry_sz = 128;
    size_t entry_array_size = entry_cnt * entry_sz; // 16KB

    // 【重要】4KBアライメントで確保する (PRP1制限回避のため)
    uint8_t *entry_buf =
        static_cast<uint8_t *>(MemoryManager::Allocate(entry_array_size, 4096));
    memset(entry_buf, 0, entry_array_size);

    GPTPartitionEntry *entries =
        reinterpret_cast<GPTPartitionEntry *>(entry_buf);

    // エントリ0設定 (変更なし)
    entries[0].type_guid = kEspGuid;
    entries[0].unique_guid = kUniqueGuid;
    entries[0].first_lba = 2048;
    entries[0].last_lba = total_blocks - 34;
    entries[0].attributes = 0;
    const char *name = "Sylphia System";
    for (int i = 0; name[i]; ++i)
        entries[0].name[i] = name[i];

    // CRC計算 (バッファ全体に対して行うのは正解)
    uint32_t entries_crc = CalculateCRC32(entry_buf, entry_array_size);

    // ---------------------------------------------------------
    // 3. 書き込み: Primary Entries (LBA 2) - 【分割書き込み修正】
    // ---------------------------------------------------------
    // NVMeドライバがPRP List未対応のため、4KB (8セクタ) ずつ書き込む
    // 16KB / 4KB = 4回ループ
    uint32_t sectors_per_chunk = 8; // 4096 / 512
    uint32_t total_chunks = entry_array_size / 4096;

    for (uint32_t i = 0; i < total_chunks; ++i)
    {
        uint64_t lba_offset = 2 + (i * sectors_per_chunk);
        uint8_t *buf_offset = entry_buf + (i * 4096);
        NVMe::g_nvme->Write(lba_offset, buf_offset, sectors_per_chunk);
    }

    // ---------------------------------------------------------
    // 4. 書き込み: Backup Entries - 【分割書き込み修正】
    // ---------------------------------------------------------
    uint32_t entry_sectors = entry_array_size / 512;
    uint64_t backup_start_lba = total_blocks - 1 - entry_sectors;

    for (uint32_t i = 0; i < total_chunks; ++i)
    {
        uint64_t lba_offset = backup_start_lba + (i * sectors_per_chunk);
        uint8_t *buf_offset = entry_buf + (i * 4096);
        NVMe::g_nvme->Write(lba_offset, buf_offset, sectors_per_chunk);
    }

    kprintf("[Installer] Partition Entries Written (Split 4KB chunks).\n");

    // ---------------------------------------------------------
    // 5. GPT Headers (変更なし)
    // ---------------------------------------------------------
    memset(sector_buf, 0, 512);
    GPTHeader *header = reinterpret_cast<GPTHeader *>(sector_buf);

    header->signature = 0x5452415020494645;
    header->revision = 0x00010000;
    header->header_size = 92;
    header->my_lba = 1;
    header->alternate_lba = total_blocks - 1;
    header->first_usable_lba = 2048;
    header->last_usable_lba = total_blocks - 34;
    header->disk_guid = kUniqueGuid;
    header->partition_entry_lba = 2;
    header->num_partition_entries = entry_cnt;
    header->sizeof_partition_entry = entry_sz;
    header->partition_entry_array_crc32 = entries_crc;

    header->crc32 = 0;
    header->crc32 = CalculateCRC32(header, 92);
    NVMe::g_nvme->Write(1, sector_buf, 1);

    // Backup Header
    header->my_lba = total_blocks - 1;
    header->alternate_lba = 1;
    header->partition_entry_lba = backup_start_lba;
    header->crc32 = 0;
    header->crc32 = CalculateCRC32(header, 92);
    NVMe::g_nvme->Write(total_blocks - 1, sector_buf, 1);

    kprintf("[Installer] GPT Format Complete!\n");

    MemoryManager::Free(sector_buf, 512);
    MemoryManager::Free(entry_buf, entry_array_size);
}

void RunInstaller(FAT32Driver *nvme_fs, bool already_installed)
{
    if (!USB::g_mass_storage)
    {
        kprintf("[Installer] No USB Mass Storage detected.\n");
        return;
    }

    kprintf("[Installer] USB Mass Storage Detected. Checking for updates...\n");

    // ERDP確認
    if (g_xhci)
    {
        g_xhci->DebugDump();
    }

    uint8_t *buf = static_cast<uint8_t *>(MemoryManager::Allocate(512));
    kprintf(
        "[Installer] DEBUG: Calling USB::g_mass_storage->Read(0, buf, 1)...\n");
    USB::g_mass_storage->Read(0, buf, 1);
    kprintf("[Installer] DEBUG: USB Read complete.\n");

    uint64_t usb_part_lba = 0;

    if (buf[510] == 0x55 && buf[511] == 0xAA)
    {
        bool is_bpb = (buf[0] == 0xEB || buf[0] == 0xE9);

        if (!is_bpb)
        {
            uint32_t start_lba = *reinterpret_cast<uint32_t *>(&buf[0x1BE + 8]);
            kprintf("[Installer] MBR detected. Partition 1 starts at LBA %d\n",
                    start_lba);
            usb_part_lba = start_lba;
        }
        else
        {
            kprintf(
                "[Installer] BPB detected at LBA 0 (Superfloppy format).\n");
        }
    }
    MemoryManager::Free(buf, 512);

    FAT32Driver *usb_fs = new FAT32Driver(USB::g_mass_storage, usb_part_lba);
    usb_fs->Initialize();

    if (!already_installed)
    {
        kprintf("[Installer] Performing initial file copy...\n");

        // システムディレクトリ作成
        nvme_fs->EnsureDirectory("sys");
        nvme_fs->EnsureDirectory("sys/bin");
        nvme_fs->EnsureDirectory("home");

        // ファイルコピー
        nvme_fs->CopyFileFrom(usb_fs, "EFI/BOOT/BOOTX64.EFI",
                              "EFI/BOOT/BOOTX64.EFI");
        nvme_fs->CopyFileFrom(usb_fs, "apps/shell.elf", "sys/bin/shell.elf");
        nvme_fs->CopyFileFrom(usb_fs, "apps/stdio.elf", "sys/bin/stdio.elf");
        nvme_fs->CopyFileFrom(usb_fs, "apps/test.elf", "sys/bin/test.elf");
        nvme_fs->CopyFileFrom(usb_fs, "kernel.elf", "kernel.elf");

        kprintf("[Installer] Update process finished.\n");

        // スタートアップスクリプト作成
        const char *startup_script = "\\EFI\\BOOT\\BOOTX64.EFI";
        nvme_fs->WriteFile("STARTUP NSH", startup_script, 21, 0);
        kprintf("[Installer] startup.nsh created.\n");

        kprintf("[Installer] Installation Complete!\n");
    }

    delete usb_fs;
}

} // namespace FileSystem
