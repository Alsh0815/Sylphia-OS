#pragma once
#include "driver/usb/xhci.hpp"
#include "block_device.hpp"
#include <stdint.h>

namespace USB
{
    // Bulk-Only Transport (BOT) 用の構造体定義
    struct CommandBlockWrapper
    {
        uint32_t signature; // 'USBC' = 0x43425355
        uint32_t tag;       // ユニークなID
        uint32_t data_transfer_length;
        uint8_t flags;       // Bit 7: Direction (0=Out, 1=In)
        uint8_t lun;         // Logical Unit Number
        uint8_t cb_length;   // コマンド長 (1-16)
        uint8_t command[16]; // SCSIコマンド
    } __attribute__((packed));

    struct CommandStatusWrapper
    {
        uint32_t signature; // 'USBS' = 0x53425355
        uint32_t tag;       // CBWと同じID
        uint32_t data_residue;
        uint8_t status; // 0=Success, 1=Fail, 2=Phase Error
    } __attribute__((packed));

    class MassStorage : public BlockDevice
    {
    public:
        MassStorage(XHCI::Controller *controller, uint8_t slot_id);

        bool Read(uint64_t lba, void *buffer, uint32_t count) override
        {
            return ReadSectors(lba, count, buffer);
        }

        bool Write(uint64_t lba, const void *buffer, uint32_t count) override
        {
            return false;
        }

        uint32_t GetBlockSize() const override { return block_size_; }

        bool Initialize();

        bool ReadSectors(uint64_t lba, uint32_t num_sectors, void *buffer);

        uint64_t GetTotalBlocks() const { return total_blocks_; }

    private:
        XHCI::Controller *controller_;
        uint8_t slot_id_;
        uint8_t ep_bulk_in_;
        uint8_t ep_bulk_out_;

        uint64_t total_blocks_;
        uint32_t block_size_;

        // BOTプロトコル用ヘルパー
        bool SendCBW(uint32_t tag, uint32_t data_len, uint8_t flags, uint8_t lun, uint8_t cmd_len, const uint8_t *cmd);
        bool ReceiveCSW(uint32_t tag);
        bool TransferData(void *buffer, uint32_t len, bool is_in);

        // SCSIコマンド
        bool ScsiInquiry();
        bool ScsiReadCapacity();
        bool ScsiRequestSense(); // エラー時に詳細を取得する場合に使用
    };

    extern MassStorage *g_mass_storage;
}