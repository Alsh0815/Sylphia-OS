#pragma once
#include <stdint.h>
#include "../include/framebuffer.hpp"
#include "../include/font8x8.hpp"

class Painter
{
public:
    static constexpr uint32_t kCW = 8; // glyph width
    static constexpr uint32_t kCH = 8; // glyph height
    static constexpr uint32_t kCS = 1; // spacing
    static constexpr uint32_t kAdv = kCW + kCS;

    Painter(Framebuffer &fb) : fb_(fb), fg_{255, 255, 255}, bg_{0, 0, 0}, use_bg_(false) {}

    void setColor(Color fg) { fg_ = fg; }
    void setColors(Color fg, Color bg)
    {
        fg_ = fg;
        bg_ = bg;
        use_bg_ = true;
    }
    void disableBackground() { use_bg_ = false; }

    void setClip(Clip c) { fb_.setClip(c); }
    void resetClip() { fb_.resetClip(); }

    // 単一文字（背景塗り対応）
    void drawChar(uint32_t x, uint32_t y, char c)
    {
        if (use_bg_)
            fb_.fillRect(x, y, kCW, kCH, bg_);
        const Glyph *g = font_lookup(c);
        for (int r = 0; r < 8; r++)
        {
            uint8_t bits = g->rows[r];
            for (int col = 0; col < 8; ++col)
                if (bits & (0x80 >> col))
                    fb_.putPixel(x + col, y + r, fg_);
        }
    }

    void drawCharRaw(uint32_t x, uint32_t y, char c)
    {
        const Glyph *g = font_lookup(c);
        for (int r = 0; r < 8; r++)
        {
            uint8_t bits = g->rows[r];
            for (int col = 0; col < 8; ++col)
                if (bits & (0x80 >> col))
                    fb_.putPixel(x + col, y + r, fg_);
        }
    }

    void drawText(uint32_t x, uint32_t y, const char *s)
    {
        while (*s)
        {
            drawCharRaw(x, y, *s);
            x += kAdv;
            s++;
        }
    }

    void drawTextWithBg(uint32_t x, uint32_t y, const char *s, Color fg, Color bg)
    {
        // 幅を先に数える（改行なし想定の簡易版）
        uint32_t len = 0;
        for (const char *p = s; *p; ++p)
            ++len;
        fb_.fillRect(x, y, len * kAdv - 1 /*末尾の隙間を詰める*/, kCH, bg);
        Color old = fg_;
        fg_ = fg;
        for (uint32_t i = 0; i < len; i++)
        {
            drawChar(x + i * kAdv, y, s[i]);
        }
        fg_ = old;
    }

    // 改行・折返し対応（clip幅内で折返し）
    // 戻り値: 描画後の x,y（参照渡し）
    void drawTextWrap(uint32_t &x, uint32_t &y, const char *s, uint32_t clipRightX)
    {
        while (*s)
        {
            if (*s == '\n')
            {
                x = startX_;
                y += lineH_;
                ++s;
                continue;
            }
            if (x + kCW >= clipRightX)
            {
                x = startX_;
                y += lineH_;
            }
            drawChar(x, y, *s++);
            x += kAdv;
        }
    }

    void drawTextWrapBg(uint32_t &x, uint32_t &y,
                        const char *s, uint32_t clipRightX,
                        Color fg, Color bg)
    {
        // 元の前景/背景設定を保存
        Color fg_old = fg_, bg_old = bg_;
        bool bg_flag_old = use_bg_;
        fg_ = fg;

        while (*s)
        {
            // 明示改行: 行送り
            if (*s == '\n')
            {
                x = startX_;
                y += lineH_;
                ++s;
                continue;
            }

            // 今行に収まる文字数を見積もる
            uint32_t startX = x;
            uint32_t count = 0;
            const char *p = s;
            while (*p && *p != '\n')
            {
                // 次の1文字を置いた後の右端
                uint32_t nextRight = startX + (count + 1) * kAdv - 1; // 末尾の隙間を詰める
                if (nextRight >= clipRightX)
                    break;
                ++count;
                ++p;
            }
            if (count == 0)
            {
                // 1文字も入らない場合は強制改行（無限ループ回避）
                x = startX_;
                y += lineH_;
                continue;
            }

            // 背景帯を先に塗る（行全体）
            fb_.fillRect(startX, y, count * kAdv - 1, kCH, bg);

            // 文字は背景なしで重ねる
            for (uint32_t i = 0; i < count; ++i)
            {
                drawCharRaw(startX + i * kAdv, y, s[i]);
            }

            // 消費 & カーソル前進
            s += count;
            x = startX + count * kAdv;

            // 行末に到達したら改行（折り返し）
            if (*s && *s != '\n')
            {
                x = startX_;
                y += lineH_;
            }

            // 明示改行が続いていたら食べる
            if (*s == '\n')
            {
                ++s;
                x = startX_;
                y += lineH_;
            }
        }

        // 設定を戻す
        fg_ = fg_old;
        bg_ = bg_old;
        use_bg_ = bg_flag_old;
    }

    void setTextLayout(uint32_t startX, uint32_t lineH)
    {
        startX_ = startX;
        lineH_ = lineH;
    }

    // 数値
    void drawDec(uint32_t &x, uint32_t &y, uint64_t v, uint32_t clipRightX)
    {
        char buf[24];
        int i = 0;
        if (v == 0)
            buf[i++] = '0';
        while (v)
        {
            buf[i++] = char('0' + (v % 10));
            v /= 10;
        }
        for (int j = i - 1; j >= 0; --j)
        {
            if (x + kCW > clipRightX)
            {
                x = startX_;
                y += lineH_;
            }
            drawChar(x, y, buf[j]);
            x += kAdv;
        }
    }

private:
    Framebuffer &fb_;
    Color fg_, bg_;
    bool use_bg_;
    uint32_t startX_ = 8;
    uint32_t lineH_ = 10;
};
