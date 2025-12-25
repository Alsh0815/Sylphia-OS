#include "sys/std/file_descriptor.hpp"
#include "cxx.hpp"
#include "driver/usb/keyboard/keyboard.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"
#include <std/string.hpp>

extern USB::Keyboard *g_usb_keyboard;

int KeyboardFD::Read(void *buf, size_t len)
{
    if (g_usb_keyboard)
    {
        g_usb_keyboard->Update();
    }

    if (count_ == 0)
    {
        return 0;
    }

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

// ---------------------------------------------------------
// FileFD 実装
// ---------------------------------------------------------

FileFD::FileFD(const char *path)
    : buffer_(nullptr), file_size_(0), read_pos_(0), valid_(false)
{
    // パスをコピー
    int len = strlen(path);
    if (len > 127)
        len = 127;
    memcpy(path_, path, len);
    path_[len] = '\0';

    // ファイルシステムが初期化されているか確認
    if (!FileSystem::g_fat32_driver)
    {
        return;
    }

    // ファイルサイズを取得
    file_size_ = FileSystem::g_fat32_driver->GetFileSize(path);
    if (file_size_ == 0)
    {
        return; // ファイルが存在しないか空
    }

    // サイズ制限
    if (file_size_ > kMaxFileSize)
    {
        file_size_ = kMaxFileSize;
    }

    // バッファを確保してファイルを読み込み
    buffer_ = static_cast<char *>(MemoryManager::Allocate(file_size_));
    if (!buffer_)
    {
        file_size_ = 0;
        return;
    }

    uint32_t read_bytes =
        FileSystem::g_fat32_driver->ReadFile(path, buffer_, file_size_);
    if (read_bytes > 0)
    {
        file_size_ = read_bytes;
        valid_ = true;
    }
    else
    {
        MemoryManager::Free(buffer_, file_size_);
        buffer_ = nullptr;
        file_size_ = 0;
    }
}

FileFD::~FileFD()
{
    if (buffer_)
    {
        MemoryManager::Free(buffer_, file_size_);
    }
}

int FileFD::Read(void *buf, size_t len)
{
    if (!valid_ || !buffer_)
    {
        return -1;
    }

    // 残りのデータ量
    uint32_t remaining = file_size_ - read_pos_;
    if (remaining == 0)
    {
        return 0; // EOF
    }

    // 読み取るバイト数
    size_t to_read = len;
    if (to_read > remaining)
    {
        to_read = remaining;
    }

    memcpy(buf, buffer_ + read_pos_, to_read);
    read_pos_ += to_read;

    return static_cast<int>(to_read);
}
