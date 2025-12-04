#pragma once
#include "pci/pci.hpp"
#include <stdint.h>

namespace USB::XHCI
{
struct TRB
{
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

enum TRBType
{
    TRB_ENABLE_SLOT = 9,
    TRB_ADDRESS_DEVICE = 11,
    TRB_NO_OP = 23,
    TRB_COMMAND_COMPLETION = 32,
    TRB_PORT_STATUS_CHANGE = 34
};

struct EventRingSegmentTableEntry
{
    uint64_t ring_segment_base_address;
    uint32_t ring_segment_size;
    uint32_t reserved;
    uint32_t reserved2;
};

struct SlotContext
{
    uint32_t route_string : 20;
    uint32_t speed : 4;
    uint32_t reserved1 : 1;
    uint32_t mtt : 1;
    uint32_t hub : 1;
    uint32_t context_entries : 5;

    uint32_t max_exit_latency : 16;
    uint32_t root_hub_port_num : 8;
    uint32_t num_ports : 8;

    uint32_t tt_hub_slot_id : 8;
    uint32_t tt_port_num : 8;
    uint32_t ttt : 2;
    uint32_t reserved2 : 4;
    uint32_t interrupter_target : 10;

    uint32_t usb_device_address : 8;
    uint32_t reserved3 : 19;
    uint32_t slot_state : 5;

    uint32_t reserved4[4];
};

struct EndpointContext
{
    uint32_t ep_state : 3;
    uint32_t reserved1 : 5;
    uint32_t mult : 2;
    uint32_t max_pstreams : 5;
    uint32_t lsa : 1;
    uint32_t interval : 8;
    uint32_t max_esit_payload_hi : 8;

    uint32_t reserved2 : 1;
    uint32_t error_count : 2;
    uint32_t ep_type : 3;
    uint32_t reserved3 : 1;
    uint32_t host_init_disable : 1;
    uint32_t max_burst_size : 8;
    uint32_t max_packet_size : 16;

    uint64_t dequeue_pointer;

    uint32_t average_trb_length : 16;
    uint32_t max_esit_payload_lo : 16;

    uint32_t reserved4[3];
};

struct DeviceContext
{
    SlotContext slot_context;
    EndpointContext ep_contexts[31];
};

struct InputControlContext
{
    uint32_t drop_context_flags;
    uint32_t add_context_flags;
    uint32_t reserved1[5];
    uint32_t config_value : 8;
    uint32_t interface_number : 8;
    uint32_t alternate_setting : 8;
    uint32_t reserved2 : 8;
};

struct InputContext
{
    InputControlContext input_control_context;
    SlotContext slot_context;
    EndpointContext ep_contexts[31];
};

class Controller
{
  public:
    Controller(const PCI::Device &dev);

    void Initialize();

    bool ConfigureEndpoint(uint8_t slot_id, uint8_t ep_addr,
                           uint16_t max_packet_size, uint8_t interval,
                           uint8_t type);
    bool ControlIn(uint8_t slot_id, uint8_t req_type, uint8_t request,
                   uint16_t value, uint16_t index, uint16_t length,
                   void *buffer);
    int PollEndpoint(uint8_t slot_id, uint8_t ep_addr);
    bool SendNormalTRB(uint8_t slot_id, uint8_t ep_addr, void *data_buf,
                       uint32_t len);

    // 割り込みハンドラから呼ばれるEvent Ring処理
    void ProcessInterrupt();

    void DebugDump() const;

  private:
    PCI::Device pci_dev_;
    uintptr_t mmio_base_;
    uintptr_t op_regs_base_; // Operational Registers
    uintptr_t rt_regs_base_; // Runtime Registers
    uintptr_t db_regs_base_; // Doorbell Registers

    uint8_t max_slots_;

    uint64_t *dcbaa_; // Device Context Base Address Array
    TRB *command_ring_;
    TRB *event_ring_;
    EventRingSegmentTableEntry *erst_;

    uint8_t dcs_;               // Dequeue Cycle State (Event Ring用)
    uint8_t pcs_;               // Producer Cycle State (Command Ring用)
    uint32_t cmd_ring_index_;   // Command Ringの書き込み位置
    uint32_t event_ring_index_; // Event Ringの読み取り位置

    TRB *transfer_rings_[256][32];
    uint8_t ring_cycle_state_[256][32];
    uint32_t ring_index_[256][32];

    uint32_t Read32(uint32_t offset) const;
    void Write32(uint32_t offset, uint32_t value);
    uint32_t ReadOpReg(uint32_t offset) const;
    void WriteOpReg(uint32_t offset, uint32_t value);
    uint32_t ReadRtReg(uint32_t offset) const;
    void WriteRtReg(uint32_t offset, uint32_t value);

    void BiosHandoff();
    void ResetController();

    bool AddressDevice(uint8_t slot_id, int port_id, int speed);
    /*
    return: Slot ID (0x01-0xFF) on success, 0 on failure
    */
    uint8_t EnableSlot();
    void RingDoorbell(uint8_t target, uint32_t value);
    void ResetPort(int port_id);

    uint8_t AddressToDCI(uint8_t ep_addr)
    {
        if (ep_addr == 0)
            return 1;

        int ep_num = ep_addr & 0xF;
        bool is_in = (ep_addr & 0x80);
        return (2 * ep_num) + (is_in ? 1 : 0);
    }
};
} // namespace USB::XHCI

extern USB::XHCI::Controller *g_xhci;