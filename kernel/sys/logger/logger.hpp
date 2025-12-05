#pragma once

#include <stdint.h>

namespace Sys::Logger
{

// ログレベル
enum class LogLevel
{
    Info,
    Warning,
    Error,
};

// ログの種類・発生元
enum class LogType
{
    Kernel,      // カーネル全般
    FS,          // ファイルシステム
    Driver,      // ドライバ
    Memory,      // メモリ管理
    Application, // アプリケーション
    Network,     // ネットワーク (将来用)
};

// ログエントリ構造体 (メモリ内)
struct LogEntry
{
    uint64_t timestamp; // 発生時刻 (ティック数など)
    LogLevel level;     // ログレベル
    LogType type;       // ログ種類
    char message[128];  // ログメッセージ (固定長)
    bool is_flushed;    // ファイルに書き出し済みか
};

// バイナリログファイルのマジックナンバー
constexpr uint32_t kLogFileMagic = 0x474F4C53; // "SLOG" in little-endian

// Binary log file header (written once at the beginning of the file)
struct LogFileHeader
{
    uint32_t magic;       // Magic number: 0x474F4C53 ("SLOG")
    uint16_t version;     // Format version: 1
    uint16_t entry_size;  // Size of one entry (bytes)
    uint32_t entry_count; // Number of recorded entries
    uint8_t reserved[52]; // Reserved (for future extension)
};
static_assert(sizeof(LogFileHeader) == 64,
              "LogFileHeader size is not 64 bytes");

// Binary log entry (for file storage, is_flushed is excluded)
struct LogEntryBinary
{
    uint64_t timestamp;    // Timestamp
    uint8_t level;         // LogLevel (1 byte)
    uint8_t type;          // LogType (1 byte)
    uint16_t message_len;  // Message length
    char message[256];     // Message body
    uint8_t reserved[240]; // Reserved (for future extension)
};
static_assert(sizeof(LogEntryBinary) == 512,
              "LogEntryBinary size is not 512 bytes");

// Ring buffer size (number of entries)
constexpr uint32_t kLogBufferSize = 256;

// Logger class
class EventLogger
{
  public:
    EventLogger();

    // ログを追加
    void Log(LogLevel level, LogType type, const char *message);

    // ヘルパー関数
    void Info(LogType type, const char *message);
    void Warning(LogType type, const char *message);
    void Error(LogType type, const char *message);

    // ログ取得 (フィルタリング用)
    // count: 取得件数, offset: オフセット
    // filter_level: nullptrなら全て, 指定すればそのレベルのみ
    // filter_type: nullptrなら全て, 指定すればそのタイプのみ
    // keyword: nullptrなら全て, 指定すれば部分一致検索
    // 戻り値: 取得したエントリ数
    uint32_t GetLogs(LogEntry *out_entries, uint32_t count, uint32_t offset,
                     const LogLevel *filter_level = nullptr,
                     const LogType *filter_type = nullptr,
                     const char *keyword = nullptr) const;

    // 総ログ数を取得 (フィルタ適用後)
    uint32_t GetLogCount(const LogLevel *filter_level = nullptr,
                         const LogType *filter_type = nullptr,
                         const char *keyword = nullptr) const;

    // 未保存ログをファイルに書き出し
    void Flush();

    // ログレベルを文字列に変換
    static const char *LevelToString(LogLevel level);

    // ログタイプを文字列に変換
    static const char *TypeToString(LogType type);

  private:
    LogEntry buffer_[kLogBufferSize]; // リングバッファ
    uint32_t head_;                   // 次に書き込む位置
    uint32_t count_;                  // 現在のエントリ数
    uint64_t tick_counter_;           // 簡易タイムスタンプ用
};

// グローバルロガーインスタンス
extern EventLogger *g_event_logger;

// ロガー初期化
void InitializeLogger();

} // namespace Sys::Logger