#include "ioapic.hpp"

// I/O APICのベースアドレス (標準的には 0xFEC00000)
const uintptr_t kIOAPICBase = 0xFEC00000;

// レジスタインデックス
#define IOAPICID 0x00
#define IOAPICVER 0x01
#define IOREDTBL 0x10 // Redirection Table (IRQ 0〜23...)

// MMIOアクセスのためのポインタ取得
volatile uint32_t *const ioregsel = reinterpret_cast<volatile uint32_t *>(kIOAPICBase);
volatile uint32_t *const iowin = reinterpret_cast<volatile uint32_t *>(kIOAPICBase + 0x10);

uint32_t IOAPIC::Read(uint32_t index)
{
    *ioregsel = index;
    return *iowin;
}

void IOAPIC::Write(uint32_t index, uint32_t data)
{
    *ioregsel = index;
    *iowin = data;
}

void IOAPIC::Init()
{
    // 必要なら初期化処理を書くが、基本はEnableだけで動くことが多い
}

void IOAPIC::Enable(int irq, int vector, uint32_t dest_id)
{
    // Redirection Tableのエントリは64bit (Low 32bit + High 32bit)
    // IRQ n の設定レジスタは 0x10 + 2*n
    uint32_t index = IOREDTBL + 2 * irq;

    // 下位32bit設定
    // Bit 0-7: Vector (割り込み番号)
    // Bit 8-10: Delivery Mode (000 = Fixed)
    // Bit 16: Mask (0 = Enable, 1 = Disable)
    uint32_t low = vector; // Mask=0 (有効), DeliveryMode=0 (Fixed)

    // 上位32bit設定
    // Bit 56-63: Destination ID (APIC ID)
    uint32_t high = dest_id << 24;

    Write(index + 1, high);
    Write(index, low);
}