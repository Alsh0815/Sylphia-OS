#include "sys/logger/logger.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "memory/memory_manager.hpp"

namespace Sys
{
namespace Logger
{

// グローバルロガーインスタンス
EventLogger *g_event_logger = nullptr;

// 簡易的なstrlen
static int str_len(const char *s)
{
    int len = 0;
    while (*s++)
        len++;
    return len;
}

// 簡易的なstrcpy
static void str_copy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

// 部分文字列検索
static bool str_contains(const char *haystack, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    if (!haystack)
        return false;

    int h_len = str_len(haystack);
    int n_len = str_len(needle);
    if (n_len > h_len)
        return false;

    for (int i = 0; i <= h_len - n_len; i++)
    {
        bool match = true;
        for (int j = 0; j < n_len; j++)
        {
            if (haystack[i + j] != needle[j])
            {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

// コンストラクタ
EventLogger::EventLogger() : head_(0), count_(0), tick_counter_(0)
{
    for (uint32_t i = 0; i < kLogBufferSize; i++)
    {
        buffer_[i].timestamp = 0;
        buffer_[i].level = LogLevel::Info;
        buffer_[i].type = LogType::Kernel;
        buffer_[i].message[0] = '\0';
        buffer_[i].is_flushed = true;
    }
}

// ログを追加
void EventLogger::Log(LogLevel level, LogType type, const char *message)
{
    LogEntry &entry = buffer_[head_];
    entry.timestamp = tick_counter_++;
    entry.level = level;
    entry.type = type;
    str_copy(entry.message, message, sizeof(entry.message));
    entry.is_flushed = false;

    head_ = (head_ + 1) % kLogBufferSize;
    if (count_ < kLogBufferSize)
    {
        count_++;
    }
}

// ヘルパー関数
void EventLogger::Info(LogType type, const char *message)
{
    Log(LogLevel::Info, type, message);
}

void EventLogger::Warning(LogType type, const char *message)
{
    Log(LogLevel::Warning, type, message);
}

void EventLogger::Error(LogType type, const char *message)
{
    Log(LogLevel::Error, type, message);
}

// フィルタにマッチするかどうかをチェック
static bool MatchesFilter(const LogEntry &entry, const LogLevel *filter_level,
                          const LogType *filter_type, const char *keyword)
{
    if (filter_level && entry.level != *filter_level)
        return false;
    if (filter_type && entry.type != *filter_type)
        return false;
    if (keyword && !str_contains(entry.message, keyword))
        return false;
    return true;
}

// ログ取得
uint32_t EventLogger::GetLogs(LogEntry *out_entries, uint32_t count,
                              uint32_t offset, const LogLevel *filter_level,
                              const LogType *filter_type,
                              const char *keyword) const
{
    if (count_ == 0 || count == 0)
        return 0;

    uint32_t retrieved = 0;
    uint32_t skipped = 0;

    // 古い方から順に走査
    uint32_t start_idx = (count_ < kLogBufferSize) ? 0 : head_;

    for (uint32_t i = 0; i < count_; i++)
    {
        uint32_t idx = (start_idx + i) % kLogBufferSize;
        const LogEntry &entry = buffer_[idx];

        if (!MatchesFilter(entry, filter_level, filter_type, keyword))
            continue;

        if (skipped < offset)
        {
            skipped++;
            continue;
        }

        out_entries[retrieved++] = entry;
        if (retrieved >= count)
            break;
    }

    return retrieved;
}

// フィルタ適用後の総ログ数を取得
uint32_t EventLogger::GetLogCount(const LogLevel *filter_level,
                                  const LogType *filter_type,
                                  const char *keyword) const
{
    if (count_ == 0)
        return 0;

    uint32_t matched = 0;
    uint32_t start_idx = (count_ < kLogBufferSize) ? 0 : head_;

    for (uint32_t i = 0; i < count_; i++)
    {
        uint32_t idx = (start_idx + i) % kLogBufferSize;
        if (MatchesFilter(buffer_[idx], filter_level, filter_type, keyword))
            matched++;
    }

    return matched;
}

// 未保存ログをファイルにバイナリ形式で書き出し
void EventLogger::Flush()
{
    if (!FileSystem::g_system_fs)
        return;

    // 書き出すエントリ数をカウント
    uint32_t entries_to_flush = 0;
    for (uint32_t i = 0; i < count_; i++)
    {
        uint32_t idx =
            (count_ < kLogBufferSize) ? i : ((head_ + i) % kLogBufferSize);
        if (!buffer_[idx].is_flushed)
            entries_to_flush++;
    }

    if (entries_to_flush == 0)
        return;

    // ログファイルが存在するかチェックし、なければヘッダーを書き込む
    // GetFileSize は存在しないファイルに対して 0 を返す
    uint32_t existing_size =
        FileSystem::g_system_fs->GetFileSize("SYSTEM  LOG");

    if (existing_size == 0)
    {
        // Header
        LogFileHeader header;
        header.magic = kLogFileMagic; // Magic number
        header.version = 1;           // Format version
        header.entry_size = sizeof(LogEntryBinary);
        header.entry_count = 0;
        // reserved を 0 で初期化
        for (int i = 0; i < 52; i++)
            header.reserved[i] = 0;

        FileSystem::g_system_fs->WriteFile("SYSTEM  LOG", &header,
                                           sizeof(LogFileHeader), 0);
    }

    // バイナリエントリ用バッファを確保
    constexpr uint32_t kMaxEntriesPerFlush = 8; // 512バイト * 8 = 4KB
    uint32_t flush_count = (entries_to_flush > kMaxEntriesPerFlush)
                               ? kMaxEntriesPerFlush
                               : entries_to_flush;
    uint32_t buf_size = sizeof(LogEntryBinary) * flush_count;

    LogEntryBinary *bin_entries =
        static_cast<LogEntryBinary *>(MemoryManager::Allocate(buf_size, 512));
    if (!bin_entries)
        return;

    // バッファを 0 で初期化（reserved 領域含む）
    for (uint32_t i = 0; i < buf_size; i++)
        reinterpret_cast<uint8_t *>(bin_entries)[i] = 0;

    uint32_t bin_idx = 0;
    for (uint32_t i = 0; i < count_ && bin_idx < flush_count; i++)
    {
        uint32_t idx =
            (count_ < kLogBufferSize) ? i : ((head_ + i) % kLogBufferSize);
        LogEntry &entry = buffer_[idx];

        if (entry.is_flushed)
            continue;

        // LogEntry を LogEntryBinary に変換
        LogEntryBinary &bin = bin_entries[bin_idx];
        bin.timestamp = entry.timestamp;
        bin.level = static_cast<uint8_t>(entry.level);
        bin.type = static_cast<uint8_t>(entry.type);
        bin.message_len = static_cast<uint16_t>(str_len(entry.message));

        // メッセージをコピー
        for (int j = 0; j < 128; j++)
            bin.message[j] = entry.message[j];
        // 残りは 0 で埋める（既に初期化済み）

        entry.is_flushed = true;
        bin_idx++;
    }

    // ファイルに追記
    FileSystem::g_system_fs->AppendFile("SYSTEM  LOG", bin_entries,
                                        sizeof(LogEntryBinary) * bin_idx, 0);

    MemoryManager::Free(bin_entries, buf_size);
}

// ログレベルを文字列に変換
const char *EventLogger::LevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

// ログタイプを文字列に変換
const char *EventLogger::TypeToString(LogType type)
{
    switch (type)
    {
        case LogType::Kernel:
            return "Kernel";
        case LogType::FS:
            return "FS";
        case LogType::Driver:
            return "Driver";
        case LogType::Memory:
            return "Memory";
        case LogType::Application:
            return "App";
        case LogType::Network:
            return "Net";
        default:
            return "Unknown";
    }
}

// ロガー初期化
void InitializeLogger()
{
    g_event_logger = new EventLogger();
}

} // namespace Logger
} // namespace Sys
