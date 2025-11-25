#pragma once
#include <stdint.h>

namespace NVMe
{

    // 完了キューエントリ (16 bytes)
    // NVMeからの返事が入る場所
    struct CompletionQueueEntry
    {
        uint32_t dw0;
        uint32_t dw1;
        uint16_t sq_head;    // Submission Queueのどこまで処理したか
        uint16_t sq_id;      // どのSQの命令か
        uint16_t command_id; // どのコマンドへの返事か
        uint16_t status;     // 成功/失敗の結果 (Phase Tag含む)
    } __attribute__((packed));

    // 送信キューエントリ (64 bytes)
    // CPUが命令を書く場所
    struct SubmissionQueueEntry
    {
        uint8_t opcode; // 命令の種類 (Read, Write, Identify...)
        uint8_t flags;
        uint16_t command_id; // 命令の識別番号
        uint32_t nsid;       // Namespace ID (SSDのパーティションIDのようなもの)
        uint64_t reserved;
        uint64_t metadata_ptr;
        uint64_t data_ptr[2]; // データ転送先のメモリアドレス (PRP)
        uint32_t cdw10;       // コマンド固有のパラメータ
        uint32_t cdw11;
        uint32_t cdw12;
        uint32_t cdw13;
        uint32_t cdw14;
        uint32_t cdw15;
    } __attribute__((packed));

}