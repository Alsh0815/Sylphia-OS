#include "gpt.hpp"
#include "../block/block_device.hpp"
#include "../../console.hpp"
#include "../../pmm.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

    // GPT on-disk structures (packed)
    struct __attribute__((packed)) GptHeader
    {
        uint64_t signature;    // "EFI PART" = 0x5452415020494645
        uint32_t revision;     // 0x00010000
        uint32_t header_size;  // >= 92
        uint32_t header_crc32; // CRC over header_size with this field zeroed
        uint32_t reserved;
        uint64_t current_lba;
        uint64_t backup_lba;
        uint64_t first_usable_lba;
        uint64_t last_usable_lba;
        gpt::Guid disk_guid;
        uint64_t partition_entries_lba;
        uint32_t num_partition_entries;
        uint32_t sizeof_partition_entry; // 128 recommended
        uint32_t partition_entries_crc32;
        // rest of the 512B sector is reserved/zeros
    };

    static_assert(offsetof(GptHeader, signature) == 0, "sig offset");
    static_assert(offsetof(GptHeader, revision) == 8, "rev offset");
    static_assert(offsetof(GptHeader, header_size) == 12, "hsz offset");
    static_assert(offsetof(GptHeader, header_crc32) == 16, "hcrc offset");
    static_assert(offsetof(GptHeader, current_lba) == 24, "cur offset");
    static_assert(offsetof(GptHeader, first_usable_lba) == 40, "fulba offset");
    static_assert(offsetof(GptHeader, last_usable_lba) == 48, "lulba offset");
    static_assert(offsetof(GptHeader, disk_guid) == 56, "guid offset");
    static_assert(offsetof(GptHeader, partition_entries_lba) == 72, "pelba offset");
    static_assert(offsetof(GptHeader, num_partition_entries) == 80, "npe offset");
    static_assert(offsetof(GptHeader, sizeof_partition_entry) == 84, "spe offset");
    static_assert(offsetof(GptHeader, partition_entries_crc32) == 88, "pecrc offset");
    static_assert(sizeof(GptHeader) <= 512, "GPT header fits in 512B");

    // Partition entry (128B)
    struct __attribute__((packed)) GptEntry
    {
        gpt::Guid type_guid;
        gpt::Guid unique_guid;
        uint64_t starting_lba;
        uint64_t ending_lba; // inclusive
        uint64_t attributes;
        uint16_t name_utf16le[36]; // 72B, not NUL-terminated
    };
    static_assert(sizeof(GptEntry) == 128, "GptEntry must be 128 bytes");

    // CRC32 (IEEE 802.3) — small, table-less (bitwise) to avoid heavy .rodata
    static uint32_t crc32_ieee(const void *data, size_t len, uint32_t seed = 0xFFFFFFFFu)
    {
        uint32_t crc = seed;
        const uint8_t *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b)
            {
                uint32_t mask = -(int)(crc & 1u);
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    inline uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }

} // namespace

namespace gpt
{

    bool is_zero_guid(const Guid &g)
    {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&g);
        uint8_t acc = 0;
        for (size_t i = 0; i < 16; i++)
            acc |= p[i];
        return acc == 0;
    }

    // 4KiB論理ブロックAPIで 512B オフセットにアクセスするヘルパ
    // - off_bytes: 先頭からのバイトオフセット
    // - bytes    : 読みたいバイト数（<=4096の想定、必要に応じ分割）
    static bool read_bytes(BlockDevice &dev, uint64_t off_bytes, void *dst, size_t bytes, Console &con)
    {
        const uint64_t blk = off_bytes / 4096u;
        const size_t inblk = (size_t)(off_bytes % 4096u);
        uint8_t *out = (uint8_t *)dst;

        size_t remain = bytes;
        while (remain > 0)
        {
            uint8_t *page = (uint8_t *)pmm::alloc_pages(1);
            if (!page)
            {
                con.println("GPT: temp page alloc failed");
                return false;
            }
            uint64_t lba4k = (uint64_t)(blk + ((bytes - remain) + inblk) / 4096u); // recompute per loop
            if (!dev.read_blocks_4k(lba4k, 1, page, 4096, con))
                return false;

            size_t offset_in_page = (lba4k == blk) ? inblk : 0;
            size_t copy = min_u64(remain, 4096u - offset_in_page);
            std::memcpy(out, page + offset_in_page, copy);
            out += copy;
            remain -= copy;
            // pmm::free_pages(page, 1); // 解放APIがあれば使用
        }
        return true;
    }

    bool scan(BlockDevice &dev,
              PartitionInfo *out_parts, size_t max_parts,
              size_t *out_found,
              GptMeta *out_meta,
              Console &con)
    {
        if (out_found)
            *out_found = 0;
        // 物理セクタサイズ（512/4096）を取得
        const uint32_t ssz = dev.physical_sector_bytes();
        if (ssz == 0 || (ssz & (ssz - 1)) != 0 || ssz < 512)
        {
            con.printf("GPT: unsupported sector size=%u\n", (unsigned)ssz);
            return false;
        }

        // LBA1 に GPT ヘッダ（PMBR は LBA0、今回は未検証）
        const uint64_t hdr_off = (uint64_t)ssz; // 1 * sector
        uint8_t hdr_buf[512];
        if (!read_bytes(dev, hdr_off, hdr_buf, sizeof(hdr_buf), con))
        {
            con.println("GPT: read header failed");
            return false;
        }

        const GptHeader *h = reinterpret_cast<const GptHeader *>(hdr_buf);
        const uint64_t sig = 0x5452415020494645ull; // "EFI PART"
        if (h->signature != sig)
        {
            con.println("GPT: bad signature");
            return false;
        }
        if (h->header_size < 92 || h->header_size > 512)
        {
            con.printf("GPT: invalid header_size=%u\n", (unsigned)h->header_size);
            return false;
        }

        // ヘッダ CRC 検証
        uint8_t tmp[512];
        std::memcpy(tmp, hdr_buf, 512);
        reinterpret_cast<GptHeader *>(tmp)->header_crc32 = 0;
        uint32_t calc_hcrc = crc32_ieee(tmp, h->header_size);
        if (calc_hcrc != h->header_crc32)
        {
            con.printf("GPT: header CRC mismatch (calc=%x stored=%x)\n",
                       (unsigned)calc_hcrc, (unsigned)h->header_crc32);
            return false;
        }

        if (h->sizeof_partition_entry < sizeof(GptEntry) || (h->sizeof_partition_entry % 8) != 0)
        {
            con.printf("GPT: unsupported entry size=%u\n", (unsigned)h->sizeof_partition_entry);
            return false;
        }
        if (h->num_partition_entries == 0)
        {
            con.println("GPT: no entries");
            return false;
        }

        // エントリテーブル全体の CRC を検証しながら、パーティションを抽出
        const uint64_t entries_bytes = (uint64_t)h->sizeof_partition_entry * (uint64_t)h->num_partition_entries;
        const uint64_t entries_off = h->partition_entries_lba * (uint64_t)ssz;

        // 逐次 4KiB 読みで CRC を計算（大きな一括バッファを避ける）
        uint32_t pe_crc = 0xFFFFFFFFu;
        uint64_t processed = 0;

        // 一方で、out_parts に入れるための実パースも行う（対象が GptEntry 128B のときのみ）
        const bool parse_direct = (h->sizeof_partition_entry == sizeof(GptEntry));

        size_t out_idx = 0;
        uint64_t off = 0;
        while (off < entries_bytes)
        {
            uint8_t *page = (uint8_t *)pmm::alloc_pages(1);
            if (!page)
            {
                con.println("GPT: temp page alloc failed (entries)");
                return false;
            }

            size_t to_read = (size_t)min_u64(4096u, entries_bytes - off);
            if (!read_bytes(dev, entries_off + off, page, to_read, con))
            {
                con.println("GPT: read entries failed");
                return false;
            }
            // CRC update
            const uint8_t *p = page;
            size_t remain = to_read;
            pe_crc ^= 0xFFFFFFFFu;
            for (size_t i = 0; i < remain; i++)
            {
                uint32_t x = (pe_crc ^ p[i]) & 0xFFu;
                for (int b = 0; b < 8; b++)
                {
                    uint32_t mask = -(int)(x & 1u);
                    x = (x >> 1) ^ (0xEDB88320u & mask);
                }
                pe_crc = (pe_crc >> 8) ^ x; // ここは簡略化した逐次版でも可、ただし整合性を保つ
            }
            pe_crc ^= 0xFFFFFFFFu;

            // 実パース（128B固定時のみ安全に行う）
            if (parse_direct)
            {
                size_t cnt = to_read / sizeof(GptEntry);
                const GptEntry *ge = reinterpret_cast<const GptEntry *>(page);
                for (size_t i = 0; i < cnt && out_idx < max_parts; ++i, ++ge)
                {
                    if (is_zero_guid(ge->type_guid))
                        continue;

                    // LBA→4KiB の切り上げ/切り捨て
                    uint64_t first_byte = ge->starting_lba * (uint64_t)ssz;
                    uint64_t endp1_byte = (ge->ending_lba + 1ull) * (uint64_t)ssz; // inclusive -> end+1
                    uint64_t first4k = first_byte / 4096u;
                    uint64_t end4k = (endp1_byte + 4095u) / 4096u; // ceil
                    if (end4k > first4k)
                    {
                        out_parts[out_idx].first_lba4k = first4k;
                        out_parts[out_idx].blocks4k = end4k - first4k;
                        ++out_idx;
                    }
                }
            }

            off += to_read;
            processed += to_read;
            // pmm::free_pages(page, 1); // 解放APIがあれば使用
        }

        if (pe_crc != h->partition_entries_crc32)
        {
            con.printf("GPT: entries CRC mismatch (calc=%08x stored=%08x)\n",
                       (unsigned)pe_crc, (unsigned)h->partition_entries_crc32);
            return false;
        }

        if (out_found)
            *out_found = out_idx;
        if (out_meta)
        {
            out_meta->header_lba = h->current_lba;
            out_meta->entries_lba = h->partition_entries_lba;
            out_meta->entry_size = h->sizeof_partition_entry;
            out_meta->entry_count = h->num_partition_entries;
        }
        con.printf("GPT: scan OK (parts=%u, entry_size=%u, count=%u)\n",
                   (unsigned)out_idx, (unsigned)h->sizeof_partition_entry, (unsigned)h->num_partition_entries);
        return true;
    }

} // namespace gpt
