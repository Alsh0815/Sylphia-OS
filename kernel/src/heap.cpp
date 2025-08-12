#include "heap.hpp"
#include "pmm.hpp"
#include <stdint.h>

namespace
{

    constexpr uint64_t ALIGN = 16;
    constexpr uint64_t PAGE = 4096;

    constexpr uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

    // ----- ブロック構造 -----
    // size_and_flags: 下位1bit = 1なら使用中、サイズは16Bアライン
    struct BlockHeader
    {
        uint64_t size_and_flags;
        BlockHeader *prev_free;
        BlockHeader *next_free;
    };
    struct BlockFooter
    {
        uint64_t size;
    };

    inline uint64_t blk_size(const BlockHeader *h) { return h->size_and_flags & ~0xFULL; }
    inline bool blk_used(const BlockHeader *h) { return (h->size_and_flags & 1ull) != 0; }
    inline void blk_mark(BlockHeader *h, uint64_t sz, bool used)
    {
        h->size_and_flags = (sz & ~0xFULL) | (used ? 1ull : 0ull);
    }
    inline BlockFooter *blk_footer(BlockHeader *h)
    {
        return (BlockFooter *)((uint8_t *)h + blk_size(h) - sizeof(BlockFooter));
    }
    inline BlockHeader *blk_next(BlockHeader *h)
    {
        return (BlockHeader *)((uint8_t *)h + blk_size(h));
    }
    inline BlockHeader *blk_prev(BlockHeader *h)
    {
        // 直前のフッタから前ブロックサイズを取得
        BlockFooter *pf = (BlockFooter *)((uint8_t *)h - sizeof(BlockFooter));
        uint64_t psz = pf->size;
        return (BlockHeader *)((uint8_t *)h - psz);
    }

    // 最小ブロックサイズ（ヘッダ+フッタ+最小ペイロード=16B）
    constexpr uint64_t MIN_BLOCK = align_up(sizeof(BlockHeader) + sizeof(BlockFooter) + 16, ALIGN);

    // ----- フリーリスト（循環双方向、番兵つき） -----
    BlockHeader g_free_sentinel; // size=0, used=1, prev/next 自己指し
    uint64_t g_capacity = 0;
    uint64_t g_used = 0;
    uint64_t g_chunk_size = 128ull * 1024; // 既定拡張チャンク 128 KiB

    inline void flist_init()
    {
        g_free_sentinel.prev_free = &g_free_sentinel;
        g_free_sentinel.next_free = &g_free_sentinel;
        blk_mark(&g_free_sentinel, 0, true);
    }
    inline bool flist_empty()
    {
        return g_free_sentinel.next_free == &g_free_sentinel;
    }
    inline void flist_insert(BlockHeader *h)
    {
        h->prev_free = &g_free_sentinel;
        h->next_free = g_free_sentinel.next_free;
        g_free_sentinel.next_free->prev_free = h;
        g_free_sentinel.next_free = h;
    }
    inline void flist_remove(BlockHeader *h)
    {
        h->prev_free->next_free = h->next_free;
        h->next_free->prev_free = h->prev_free;
        h->prev_free = h->next_free = nullptr;
    }

    inline void *mem_set(void *dst, uint8_t v, uint64_t n)
    {
        uint8_t *p = (uint8_t *)dst;
        while (n--)
            *p++ = v;
        return dst;
    }
    inline void *mem_copy(void *dst, const void *src, uint64_t n)
    {
        const uint8_t *s = (const uint8_t *)src;
        uint8_t *d = (uint8_t *)dst;
        if (d == s || n == 0)
            return dst;
        if (d < s)
        {
            while (n--)
                *d++ = *s++;
        }
        else
        {
            d += n;
            s += n;
            while (n--)
                *--d = *--s;
        }
        return dst;
    }

    // フリーブロック化＆リスト挿入（サイズ/フッタ更新込み）
    inline void make_free(BlockHeader *h, uint64_t sz)
    {
        blk_mark(h, sz, false);
        blk_footer(h)->size = sz;
        flist_insert(h);
    }

    // スプリット（残りが最小ブロック未満なら分割しない）
    inline BlockHeader *split_if_big(BlockHeader *h, uint64_t needed)
    {
        uint64_t sz = blk_size(h);
        if (sz >= needed + MIN_BLOCK)
        {
            // 前半=needed（割当）、後半=残り（フリー）
            uint64_t rest = sz - needed;
            BlockHeader *nh = (BlockHeader *)((uint8_t *)h + needed);
            make_free(nh, rest);
            blk_mark(h, needed, blk_used(h));
            blk_footer(h)->size = needed;
        }
        return h;
    }

    // 併合（hはフリー前提）
    inline BlockHeader *coalesce(BlockHeader *h)
    {
        // 後方併合
        BlockHeader *nxt = blk_next(h);
        if (!blk_used(nxt) && nxt != &g_free_sentinel)
        {
            // nxt はフリーリストにあるので取り外す
            flist_remove(nxt);
            uint64_t nsz = blk_size(h) + blk_size(nxt);
            blk_mark(h, nsz, false);
            blk_footer(h)->size = nsz;
        }
        // 前方併合
        // 先頭に番兵は置いていないため、prevの有効性はサイズから判断
        BlockFooter *pf = (BlockFooter *)((uint8_t *)h - sizeof(BlockFooter));
        // 範囲チェックは省略（アリーナ管理で保護される前提）
        BlockHeader *prv = (BlockHeader *)((uint8_t *)h - pf->size);
        if (!blk_used(prv) && prv != &g_free_sentinel)
        {
            flist_remove(prv);
            uint64_t nsz = blk_size(prv) + blk_size(h);
            blk_mark(prv, nsz, false);
            blk_footer(prv)->size = nsz;
            h = prv;
        }
        return h;
    }

    // PMMからアリーナ拡張
    bool grow_arena(uint64_t min_bytes)
    {
        uint64_t want = (min_bytes > g_chunk_size) ? min_bytes : g_chunk_size;
        uint64_t pages = align_up(want, PAGE) / PAGE;
        void *mem = pmm::alloc_pages(pages);
        if (!mem)
            return false;
        uint64_t bytes = pages * PAGE;
        g_capacity += bytes;

        // 新規領域全体を1つのフリーブロックに
        BlockHeader *h = (BlockHeader *)mem;
        make_free(h, bytes);
        // 隣接する既存最後尾と自然に併合される場合もある（free時にcoalesce）
        return true;
    }

    // 最小限の「実Blockサイズ計算」（ヘッダ＋フッタ含む）
    inline uint64_t req_block_size(uint64_t payload, uint64_t align)
    {
        if (align < ALIGN)
            align = ALIGN;
        // ヘッダ直後を align に合わせる（今回はヘッダ自体が16B境界ならペイロード開始も16B境界）
        uint64_t need = align_up(sizeof(BlockHeader) + payload + sizeof(BlockFooter), ALIGN);
        return (need < MIN_BLOCK) ? MIN_BLOCK : need;
    }

} // anon

namespace heap
{

    bool init(uint64_t initial_bytes)
    {
        if (initial_bytes == 0)
            initial_bytes = 256ull * 1024;
        flist_init();
        g_capacity = 0;
        g_used = 0;
        return grow_arena(align_up(initial_bytes, PAGE));
    }

    void set_chunk_size(uint64_t bytes)
    {
        if (bytes >= 64 * 1024)
            g_chunk_size = align_up(bytes, PAGE); // 下限は64KiBに
    }

    void *kmalloc(size_t size, size_t align, bool zero)
    {
        if (size == 0)
            return nullptr;
        if (align < ALIGN)
            align = ALIGN;

        uint64_t need = req_block_size(size, align);

    retry:
        // First-Fit
        BlockHeader *cur = g_free_sentinel.next_free;
        while (cur != &g_free_sentinel)
        {
            if (!blk_used(cur) && blk_size(cur) >= need)
            {
                // 採用
                flist_remove(cur);
                cur = split_if_big(cur, need);
                blk_mark(cur, blk_size(cur), true);
                // フッタは保持済み
                g_used += blk_size(cur);
                void *user = (uint8_t *)cur + sizeof(BlockHeader);
                if (zero)
                    mem_set(user, 0, blk_size(cur) - sizeof(BlockHeader) - sizeof(BlockFooter));
                return user;
            }
            cur = cur->next_free;
        }
        // 足りない → 拡張して再試行
        if (grow_arena(need))
            goto retry;
        return nullptr;
    }

    void kfree(void *p)
    {
        if (!p)
            return;
        BlockHeader *h = (BlockHeader *)((uint8_t *)p - sizeof(BlockHeader));
        if (!blk_used(h))
            return; // 二重解放ガード（簡易）

        g_used -= blk_size(h);
        blk_mark(h, blk_size(h), false);
        blk_footer(h)->size = blk_size(h);
        // いったん挿入 → すぐ併合して、結果を再挿入し直すより、
        // まず挿入してから coalesce し、coalesce で必要な remove をやる
        flist_insert(h);
        h = coalesce(h);
        // coalesce 後の h がフリーリストに無い可能性があるので、念のため再挿入（重複防止で一旦外す）
        // （coalesce 内ですでに挿入済みなら remove→insert でもOK。ここでは簡素化のため再配置）
        flist_remove(h);
        flist_insert(h);
    }

    void *krealloc(void *p, size_t new_size)
    {
        if (p == nullptr)
            return kmalloc(new_size);
        if (new_size == 0)
        {
            kfree(p);
            return nullptr;
        }

        BlockHeader *h = (BlockHeader *)((uint8_t *)p - sizeof(BlockHeader));
        uint64_t old_sz = blk_size(h);
        uint64_t need = req_block_size(new_size, ALIGN);

        // その場拡張：後方がフリーで併せて足りるなら結合
        BlockHeader *nxt = blk_next(h);
        if (!blk_used(nxt))
        {
            uint64_t comb = old_sz + blk_size(nxt);
            if (comb >= need)
            {
                // 後方フリーを取り外して結合
                flist_remove(nxt);
                blk_mark(h, comb, true);
                blk_footer(h)->size = comb;
                // 必要ならスプリット
                split_if_big(h, need);
                g_used += (blk_size(h) - old_sz);
                return (uint8_t *)h + sizeof(BlockHeader);
            }
        }

        // 新規確保→コピー→旧解放
        void *np = kmalloc(new_size, ALIGN, false);
        if (!np)
            return nullptr;
        // コピー長：旧ペイロードの最大（フッタ/ヘッダ除く）
        uint64_t copy_n = old_sz - sizeof(BlockHeader) - sizeof(BlockFooter);
        if (copy_n > new_size)
            copy_n = new_size;
        mem_copy(np, p, copy_n);
        kfree(p);
        return np;
    }

    uint64_t capacity() { return g_capacity; }
    uint64_t used() { return g_used; }
    uint64_t remain() { return (g_capacity >= g_used) ? (g_capacity - g_used) : 0; }

} // namespace heap
