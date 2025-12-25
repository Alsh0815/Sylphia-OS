#pragma once

#include "../../console.hpp"
#include "../../printk.hpp"
#include <stddef.h>
#include <stdint.h>

// 前方宣言
namespace USB
{
class Keyboard;
}

enum FDType
{
    FD_UNKNOWN = 0,
    FD_CONSOLE,
    FD_KEYBOARD,
    FD_PIPE,
    FD_FILE
};

class FileDescriptor
{
  public:
    virtual ~FileDescriptor() = default;
    virtual int Read(void *buf, size_t len) = 0;
    virtual int Write(const void *buf, size_t len) = 0;
    virtual void Flush() {} // Default empty implementation
    virtual FDType GetType() const = 0;
};

// ---------------------------------------------------------
// Console File Descriptor (Stdout / Stderr)
// ---------------------------------------------------------
class ConsoleFD : public FileDescriptor
{
  public:
    ConsoleFD() {}

    int Read(void *buf, size_t len) override
    {
        return 0; // Console cannot be read from
    }

    int Write(const void *buf, size_t len) override
    {
        if (!g_console)
            return 0;

        const char *s = static_cast<const char *>(buf);
        // kprintfはフォーマット文字列を取るので、単純出力には向かないが、
        // ここでは一旦PutStringを直接呼ぶか、1文字ずつ出力する
        // g_console->PutStringはnull終端を期待するので、len分だけループして出力する
        for (size_t i = 0; i < len; ++i)
        {
            char c = s[i];
            char str[2] = {c, 0};
            g_console->PutString(str);
        }
        return len;
    }

    FDType GetType() const override
    {
        return FD_CONSOLE;
    }
};

// ---------------------------------------------------------
// Keyboard File Descriptor (Stdin)
// ---------------------------------------------------------
class KeyboardFD : public FileDescriptor
{
  private:
    static const int kBufferSize = 1024;
    char buffer_[kBufferSize];
    int write_pos_ = 0;
    int read_pos_ = 0;
    int count_ = 0;

  public:
    KeyboardFD() {}

    // キーボードドライバから呼ばれる入力用メソッド
    void OnInput(char c)
    {
        if (count_ >= kBufferSize)
            return; // Buffer full

        buffer_[write_pos_] = c;
        write_pos_ = (write_pos_ + 1) % kBufferSize;
        count_++;
    }

    int Read(void *buf, size_t len) override;

    int Write(const void *buf, size_t len) override
    {
        return 0; // Keyboard cannot be written to
    }

    void Flush() override
    {
        read_pos_ = 0;
        write_pos_ = 0;
        count_ = 0;
    }

    FDType GetType() const override
    {
        return FD_KEYBOARD;
    }
};

// ---------------------------------------------------------
// Pipe File Descriptor
// ---------------------------------------------------------
class PipeFD : public FileDescriptor
{
  private:
    static const int kBufferSize = 4096; // 4KB Pipe Buffer
    char *buffer_;
    int write_pos_ = 0;
    int read_pos_ = 0;
    int count_ = 0;
    bool closed_ = false;

  public:
    PipeFD()
    {
        // メモリ確保はnewで行う（カーネルヒープ）
        // ここでは簡易的にstatic配列ではなく動的確保する前提
        // ※ new演算子が使える環境であること
        buffer_ = new char[kBufferSize];
    }

    ~PipeFD()
    {
        delete[] buffer_;
    }

    int Read(void *buf, size_t len) override
    {
        if (count_ == 0)
            return 0;

        char *p = static_cast<char *>(buf);
        size_t read_count = 0;

        while (read_count < len && count_ > 0)
        {
            *p++ = buffer_[read_pos_];
            read_pos_ = (read_pos_ + 1) % kBufferSize;
            count_--;
            read_count++;
        }
        return read_count;
    }

    int Write(const void *buf, size_t len) override
    {
        const char *p = static_cast<const char *>(buf);
        size_t written_count = 0;

        while (written_count < len)
        {
            if (count_ >= kBufferSize)
                break; // Buffer full

            buffer_[write_pos_] = *p++;
            write_pos_ = (write_pos_ + 1) % kBufferSize;
            count_++;
            written_count++;
        }
        return written_count;
    }

    FDType GetType() const override
    {
        return FD_PIPE;
    }

    void Reset()
    {
        write_pos_ = 0;
        read_pos_ = 0;
        count_ = 0;
    }
};

// ---------------------------------------------------------
// File Descriptor (FAT32 File)
// ---------------------------------------------------------
namespace FileSystem
{
class FAT32Driver;
extern FAT32Driver *g_fat32_driver;
} // namespace FileSystem

class FileFD : public FileDescriptor
{
  private:
    static const uint32_t kMaxFileSize = 64 * 1024; // 最大64KB
    char *buffer_;
    uint32_t file_size_;
    uint32_t read_pos_;
    bool valid_;
    char path_[128];

  public:
    FileFD(const char *path);
    ~FileFD();

    bool IsValid() const
    {
        return valid_;
    }

    int Read(void *buf, size_t len) override;

    int Write(const void *buf, size_t len) override
    {
        // 現在は読み取り専用
        return -1;
    }

    FDType GetType() const override
    {
        return FD_FILE;
    }
};

// Global File Descriptor Table
// 0: Stdin, 1: Stdout, 2: Stderr
extern FileDescriptor *g_fds[16];
