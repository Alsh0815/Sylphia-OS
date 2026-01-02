#include "mass_storage.hpp"
#include "arch/inasm.hpp"
#include "block_device.hpp"
#include "cxx.hpp"
#include "driver/usb/usb.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"

namespace USB
{
MassStorage *g_mass_storage = nullptr;

MassStorage::MassStorage(XHCI::Controller *controller, uint8_t slot_id)
    : controller_(controller), slot_id_(slot_id), ep_bulk_in_(0),
      ep_bulk_out_(0), total_blocks_(0), block_size_(0)
{
}

bool MassStorage::Initialize()
{
    kprintf("[MSC] Initializing Slot %d...\n", slot_id_);

    // 1. コンフィギュレーションディスクリプタを取得してエンドポイントを探す
    uint8_t *buf = static_cast<uint8_t *>(
        MemoryManager::Allocate(1024, 64)); // 少し大きめに確保
    PageManager::SetDeviceMemory(buf, 1024);

    if (!controller_->ControlIn(slot_id_, 0x80, 6, 0x0200, 0, 9,
                                buf)) // まずHeaderだけ
    {
        MemoryManager::Free(buf, 1024);
        return false;
    }

    ConfigurationDescriptor *cd =
        reinterpret_cast<ConfigurationDescriptor *>(buf);
    uint16_t total_len = cd->total_length;

    if (!controller_->ControlIn(slot_id_, 0x80, 6, 0x0200, 0, total_len, buf))
    {
        MemoryManager::Free(buf, 1024);
        return false;
    }

    // 記述子をパースして Bulk IN/OUT エンドポイントを探す
    uint8_t *p = buf;
    uint8_t *end = buf + total_len;
    bool found_interface = false;

    while (p < end)
    {
        uint8_t len = p[0];
        uint8_t type = p[1];

        if (type == 4) // Interface Descriptor
        {
            InterfaceDescriptor *id =
                reinterpret_cast<InterfaceDescriptor *>(p);
            // Class=8 (Mass Storage), SubClass=6 (SCSI), Protocol=0x50
            // (Bulk-Only)
            if (id->interface_class == 0x08 &&
                id->interface_sub_class == 0x06 &&
                id->interface_protocol == 0x50)
            {
                found_interface = true;
            }
            else
            {
                found_interface = false;
            }
        }
        else if (type == 5 && found_interface) // Endpoint Descriptor
        {
            EndpointDescriptor *ed = reinterpret_cast<EndpointDescriptor *>(p);
            uint8_t addr = ed->endpoint_address;
            uint8_t attr = ed->attributes & 0x03;

            if (attr == 2) // Bulk Transfer
            {
                if (addr & 0x80) // IN
                {
                    ep_bulk_in_ = addr;
                }
                else // OUT
                {
                    ep_bulk_out_ = addr;
                }
            }
        }
        p += len;
    }
    MemoryManager::Free(buf, 1024);

    if (ep_bulk_in_ == 0 || ep_bulk_out_ == 0)
    {
        kprintf("[MSC] Failed to find Bulk Endpoints.\n");
        return false;
    }

    kprintf("[MSC] Bulk IN: %x, Bulk OUT: %x\n", ep_bulk_in_, ep_bulk_out_);

    if (!controller_->ConfigureEndpoint(slot_id_, ep_bulk_in_, 512, 0, 2))
        return false;
    if (!controller_->ConfigureEndpoint(slot_id_, ep_bulk_out_, 512, 0, 2))
        return false;

    if (!ScsiReadCapacity())
    {
        kprintf("[MSC] READ CAPACITY failed.\n");
        return false;
    }

    kprintf("[MSC] Initialization Complete. Size: %lu MB\n",
            (total_blocks_ * block_size_) / 1024 / 1024);
    return true;
}

bool MassStorage::ScsiReadCapacity()
{
    busy_ = true;

    uint8_t cmd[16] = {0};
    cmd[0] = 0x25;

    uint8_t *data = static_cast<uint8_t *>(MemoryManager::Allocate(8, 64));
    PageManager::SetDeviceMemory(data, 8);

    if (!SendCBW(1, 8, 0x80, 0, 10, cmd))
    {
        busy_ = false;
        return false;
    }

    if (!TransferData(data, 8, true))
    {
        busy_ = false;
        return false;
    }

    if (!ReceiveCSW(1))
    {
        busy_ = false;
        return false;
    }

    uint32_t last_lba =
        (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    uint32_t blk_sz =
        (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

    total_blocks_ = last_lba + 1;
    block_size_ = blk_sz;

    MemoryManager::Free(data, 8);
    busy_ = false;
    return true;
}

bool MassStorage::ReadSectors(uint64_t lba, uint32_t num_sectors, void *buffer)
{
    // Event Ring競合回避のためビジーフラグを設定
    busy_ = true;

    kprintf("[MSC DEBUG] ReadSectors: LBA=%lu, count=%u\n", lba, num_sectors);
    uint32_t bytes_len = num_sectors * block_size_;

    // データバッファの属性を変更
    PageManager::SetDeviceMemory(buffer, bytes_len);

    uint8_t cmd[16] = {0};
    cmd[0] = 0x28; // READ (10)
    // LBA (Big Endian)
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = (lba) & 0xFF;
    // Transfer Length (Blocks)
    cmd[7] = (num_sectors >> 8) & 0xFF;
    cmd[8] = (num_sectors) & 0xFF;

    kprintf("[MSC DEBUG] Sending CBW...\n");
    if (!SendCBW(2, bytes_len, 0x80, 0, 10, cmd))
    {
        busy_ = false;
        return false;
    }
    kprintf("[MSC DEBUG] CBW sent. Transferring data...\n");

    if (!TransferData(buffer, bytes_len, true))
    {
        busy_ = false;
        return false;
    }
    kprintf("[MSC DEBUG] Data transferred. Receiving CSW...\n");

    if (!ReceiveCSW(2))
    {
        busy_ = false;
        return false;
    }
    kprintf("[MSC DEBUG] CSW received. ReadSectors complete.\n");

    busy_ = false;
    return true;
}

bool MassStorage::SendCBW(uint32_t tag, uint32_t data_len, uint8_t flags,
                          uint8_t lun, uint8_t cmd_len, const uint8_t *cmd)
{
    kprintf("[MSC DEBUG] SendCBW: tag=%u, len=%u\n", tag, data_len);
    CommandBlockWrapper *cbw = static_cast<CommandBlockWrapper *>(
        MemoryManager::Allocate(sizeof(CommandBlockWrapper), 64));
    PageManager::SetDeviceMemory(cbw, sizeof(CommandBlockWrapper));

    cbw->signature = 0x43425355; // USBC
    cbw->tag = tag;
    cbw->data_transfer_length = data_len;
    cbw->flags = flags;
    cbw->lun = lun;
    cbw->cb_length = cmd_len;
    for (int i = 0; i < 16; ++i)
        cbw->command[i] = (i < cmd_len) ? cmd[i] : 0;

    kprintf("[MSC DEBUG] SendCBW: Sending TRB to EP %x...\n", ep_bulk_out_);
    bool ret = controller_->SendNormalTRB(slot_id_, ep_bulk_out_, cbw, 31);
    kprintf("[MSC DEBUG] SendCBW: TRB sent, ret=%d. Polling...\n", ret);

    int poll_count = 0;
    while (controller_->PollEndpoint(slot_id_, ep_bulk_out_) == -1)
    {
        poll_count++;
        if (poll_count > 1000000)
        {
            kprintf("[MSC DEBUG] SendCBW TIMEOUT!\n");
            MemoryManager::Free(cbw, sizeof(CommandBlockWrapper));
            return false;
        }
    }
    kprintf("[MSC DEBUG] SendCBW: Poll complete.\n");

    MemoryManager::Free(cbw, sizeof(CommandBlockWrapper));
    return ret;
}

bool MassStorage::TransferData(void *buffer, uint32_t len, bool is_in)
{
    uint8_t ep = is_in ? ep_bulk_in_ : ep_bulk_out_;
    kprintf("[MSC DEBUG] TransferData: EP=%x, len=%u, is_in=%d\n", ep, len,
            is_in);

    // 受信の場合は事前にキャッシュをインバリデートしておく
    // (キャッシュ上のゴミデータがDRAMへのDMA書き込みを阻害しないように、または読み込み時に邪魔しないように)
    if (is_in)
    {
        InvalidateCache(buffer, len);
    }

    bool ret = controller_->SendNormalTRB(slot_id_, ep, buffer, len);
    kprintf("[MSC DEBUG] TransferData: TRB sent, ret=%d. Polling...\n", ret);

    int poll_count = 0;
    while (controller_->PollEndpoint(slot_id_, ep) == -1)
    {
        poll_count++;
        if (poll_count % 100000 == 0)
            kprintf("[MSC DEBUG] TransferData Poll loop: %d iterations\n",
                    poll_count);
        if (poll_count > 1000000)
        {
            kprintf("[MSC DEBUG] TransferData TIMEOUT!\n");
            return false;
        }
    }
    kprintf("[MSC DEBUG] TransferData: Poll complete.\n");
    if (is_in)
    {
        InvalidateCache(buffer, len);
    }
    return ret;
}

bool MassStorage::ReceiveCSW(uint32_t tag)
{
    kprintf("[MSC DEBUG] ReceiveCSW: tag=%u\n", tag);
    CommandStatusWrapper *csw = static_cast<CommandStatusWrapper *>(
        MemoryManager::Allocate(sizeof(CommandStatusWrapper), 64));
    PageManager::SetDeviceMemory(csw, sizeof(CommandStatusWrapper));

    bool ret = controller_->SendNormalTRB(slot_id_, ep_bulk_in_, csw, 13);
    kprintf("[MSC DEBUG] ReceiveCSW: TRB sent, ret=%d. Polling...\n", ret);

    int poll_count = 0;
    while (controller_->PollEndpoint(slot_id_, ep_bulk_in_) == -1)
    {
        poll_count++;
        if (poll_count % 100000 == 0)
            kprintf("[MSC DEBUG] ReceiveCSW Poll loop: %d iterations\n",
                    poll_count);
        if (poll_count > 1000000)
        {
            kprintf("[MSC DEBUG] ReceiveCSW TIMEOUT!\n");
            MemoryManager::Free(csw, sizeof(CommandStatusWrapper));
            return false;
        }
    }
    kprintf("[MSC DEBUG] ReceiveCSW: Poll complete.\n");
    InvalidateCache(csw, sizeof(CommandStatusWrapper));

    if (csw->signature != 0x53425355 || csw->tag != tag || csw->status != 0)
    {
        kprintf("[MSC] CSW Error. Status=%d\n", csw->status);
        ret = false;
    }

    MemoryManager::Free(csw, sizeof(CommandStatusWrapper));
    return ret;
}
} // namespace USB