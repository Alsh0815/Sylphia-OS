#include "syscall.hpp"
#include "app/elf/elf_loader.hpp"
#include "arch/inasm.hpp"
#include "fs/fat32/fat32_driver.hpp"
#include "graphic/GraphicSystem.hpp"
#include "memory/memory_manager.hpp"
#include "paging.hpp"
#include "printk.hpp"
#include "sys.hpp"
#include "sys/std/file_descriptor.hpp"
#include "task/scheduler.hpp"
#include "task/task_manager.hpp"
#include <std/string.hpp>
#include <stdint.h>

// アプリ実行状態（elf_loader.cppで定義）
extern bool g_app_running;

// 定数定義 (MSR)
const uint32_t kMSR_EFER = 0xC0000080;
const uint32_t kMSR_STAR = 0xC0000081;
const uint32_t kMSR_LSTAR = 0xC0000082;
const uint32_t kMSR_FMASK = 0xC0000084;
const uint32_t kMSR_KERNEL_GS_BASE = 0xC0000102;

// asmfunc.asm
extern "C" void ExitApp();
#if defined(__x86_64__)
extern "C" uint64_t ReadMSR(uint32_t msr);
extern "C" void WriteMSR(uint32_t msr, uint64_t value);
extern "C" void SyscallEntry();
#endif

// コンテキストの実体
SyscallContext *g_syscall_context = nullptr;

// ■ C++側システムコールハンドラ
// アセンブリ側から呼び出される
extern "C" uint64_t SyscallHandler(uint64_t syscall_number, uint64_t arg1,
                                   uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                   uint64_t return_rip)
{
    switch (syscall_number)
    {
        case 1: // PutChar
            kprintf("%c", static_cast<char>(arg1));
            return 0;

        case 2: // Exit
        {
            kprintf("\n[Kernel] App Exited via Syscall.\n");

            // アプリ実行状態をリセット
            g_app_running = false;

            // キーボードバッファをフラッシュ（残りの入力がシェルに渡されないように）
            if (g_fds[0] && g_fds[0]->GetType() == FDType::FD_KEYBOARD)
            {
                g_fds[0]->Flush();
            }

            // マルチタスク環境かどうかを確認
            Task *current = TaskManager::GetCurrentTask();
            if (current && current->is_app)
            {
                // 重要: Exit処理中は割り込みを無効化
                // (タイマー割り込みでIdleTaskに切り替わるのを防ぐ)
                CLI();

                // 重要: タスク情報を先にローカル変数に保存
                // (キューから削除後にcurrentが無効になる可能性があるため)
                uint64_t task_cr3 = current->context.cr3;
                char **task_argv = current->argv;
                int task_argc = current->argc;

                // カーネルのページテーブルに戻す
                PageManager::SwitchPageTable(PageManager::GetKernelCR3());

                // タスクをキューから削除
                TaskManager::RemoveFromReadyQueue(current);
                current->state = TaskState::TERMINATED;
                TaskManager::SetCurrentTask(nullptr);

                // プロセス専用ページテーブルを解放
                uint64_t kernel_cr3 = PageManager::GetKernelCR3();
                bool should_free = (task_cr3 != 0 && task_cr3 != kernel_cr3);
                if (should_free)
                {
                    PageManager::FreeProcessPageTable(task_cr3);
                }

                // argv配列を解放
                if (task_argv)
                {
                    for (int i = 0; i < task_argc; ++i)
                    {
                        if (task_argv[i])
                        {
                            MemoryManager::Free(task_argv[i],
                                                strlen(task_argv[i]) + 1);
                        }
                    }
                    MemoryManager::Free(task_argv,
                                        sizeof(char *) * (task_argc + 1));
                }

                // レガシー方式でカーネルに戻る（スタック解放はしない）
                // ExitAppはg_kernel_rsp_saveに保存されたカーネルスタックに戻る
                ExitApp();
            }
            else
            {
                // レガシー: 旧式のExitApp()を呼ぶ
                ExitApp();
            }

            // ここには戻ってこない
            while (1)
                Hlt();
            return 0;
        }

        case 3: // ListDirectory (ls)
            // arg1: cluster (0=Root)
            if (FileSystem::g_fat32_driver)
            {
                FileSystem::g_fat32_driver->ListDirectory(
                    static_cast<uint32_t>(arg1));
            }
            return 0;

        case 4: // ReadFile
            // arg1: filename (char*)
            // arg2: buffer (void*)
            // arg3: buffer_size (uint32_t)
            // 戻り値: 読み込んだバイト数 (uint64_t)
            if (FileSystem::g_fat32_driver)
            {
                const char *name = reinterpret_cast<const char *>(arg1);
                void *buf = reinterpret_cast<void *>(arg2);
                uint32_t len = static_cast<uint32_t>(arg3);
                return FileSystem::g_fat32_driver->ReadFile(name, buf, len);
            }
            return 0;

        case 5: // Read (fd, buf, len)
        {
            int fd = static_cast<int>(arg1);
            void *buf = reinterpret_cast<void *>(arg2);
            size_t len = static_cast<size_t>(arg3);
            if (fd >= 0 && fd < 16 && g_fds[fd])
            {
                int ret = g_fds[fd]->Read(buf, len);
                return ret;
            }
            return -1;
        }

        case 6: // Write (fd, buf, len)
        {
            int fd = static_cast<int>(arg1);
            const void *buf = reinterpret_cast<const void *>(arg2);
            size_t len = static_cast<size_t>(arg3);

            if (fd >= 0 && fd < 16 && g_fds[fd])
            {
                char kernel_buf[256];
                const char *user_buf = static_cast<const char *>(buf);
                for (size_t i = 0; i < len && i < 255; ++i)
                {
                    kernel_buf[i] = user_buf[i];
                }
                kernel_buf[len < 255 ? len : 255] = '\0';
                int ret = g_fds[fd]->Write(kernel_buf, len);
                // delete[] kernel_buf;
                return ret;
            }
            return -1;
        }

        case 10: // Yield (自発的にCPUを手放す)
            Scheduler::Yield();
            return 0;

        case 11: // TaskExit (タスク終了)
        {
            Task *current = TaskManager::GetCurrentTask();
            if (current)
            {
                TaskManager::TerminateTask(current);
                TaskManager::SetCurrentTask(nullptr);
                Scheduler::Schedule(); // 次のタスクへ
            }
            // ここには戻ってこない
            return 0;
        }

        case 20: // Spawn (プロセス起動)
        {
            // arg1: path (char*) - ユーザー空間
            // arg2: argc (int)
            // arg3: argv (char**) - ユーザー空間
            // 戻り値: タスクID (成功) または 0 (失敗)

            // 重要: CreateProcess内でページテーブルが切り替わるため、
            // ユーザー空間のポインタは無効になる。
            // そのため、ここでカーネル空間にコピーする。

            const char *user_path = reinterpret_cast<const char *>(arg1);
            int argc = static_cast<int>(arg2);
            char **user_argv = reinterpret_cast<char **>(arg3);

            // pathをカーネル空間にコピー
            char kernel_path[256];
            strcpy(kernel_path, user_path);

            // argvをカーネル空間にコピー
            char *kernel_argv[32];
            static char argv_buffer[32][256]; // 静的バッファ（簡易実装）

            if (argc > 32)
                argc = 32;

            for (int i = 0; i < argc; ++i)
            {
                if (user_argv[i])
                {
                    strcpy(argv_buffer[i], user_argv[i]);
                    kernel_argv[i] = argv_buffer[i];
                }
                else
                {
                    kernel_argv[i] = nullptr;
                }
            }

            kprintf("[Syscall] Spawn: %s %d %p\n", kernel_path, argc,
                    kernel_argv);

            Task *task =
                ElfLoader::CreateProcess(kernel_path, argc, kernel_argv);
            if (task)
            {
                return task->task_id;
            }
            return 0;
        }

        case 21: // Open (ファイルオープン)
        {
            // arg1: path (char*)
            // arg2: flags (int) - 現在は未使用
            // 戻り値: fd (成功) または -1 (失敗)
            const char *path = reinterpret_cast<const char *>(arg1);

            // 空きfdを探す (3以降を使用、0-2は標準I/O)
            for (int fd = 3; fd < 16; ++fd)
            {
                if (g_fds[fd] == nullptr)
                {
                    // FileFDを作成
                    FileFD *file_fd = new FileFD(path);
                    if (file_fd->IsValid())
                    {
                        g_fds[fd] = file_fd;
                        return fd;
                    }
                    delete file_fd;
                    return -1;
                }
            }
            return -1; // fdが足りない
        }

        case 22: // Close (ファイルクローズ)
        {
            // arg1: fd (int)
            // 戻り値: 0 (成功) または -1 (失敗)
            int fd = static_cast<int>(arg1);
            if (fd >= 3 && fd < 16 && g_fds[fd])
            {
                delete g_fds[fd];
                g_fds[fd] = nullptr;
                return 0;
            }
            return -1;
        }

        case 23: // DeleteFile (ファイル削除)
        {
            // arg1: path (char*)
            // 戻り値: 0 (成功) または -1 (失敗)
            const char *path = reinterpret_cast<const char *>(arg1);
            if (FileSystem::g_fat32_driver)
            {
                if (FileSystem::g_fat32_driver->DeleteFile(path))
                {
                    return 0;
                }
            }
            return -1;
        }

        case 30: // GetDisplayInfo (ディスプレイ情報取得)
        {
            // arg1: DisplayInfo* (ユーザー空間のバッファ)
            // arg2: max_count (int)
            // 戻り値: ディスプレイ数
            struct DisplayInfo
            {
                uint32_t id;
                uint32_t width;
                uint32_t height;
                uint8_t render_mode;
                uint8_t padding[3];
            };

            DisplayInfo *user_buf = reinterpret_cast<DisplayInfo *>(arg1);
            int max_count = static_cast<int>(arg2);

            if (!Graphic::g_display_manager)
                return 0;

            size_t count = Graphic::g_display_manager->GetDisplayCount();
            int copy_count =
                (count < static_cast<size_t>(max_count)) ? count : max_count;

            for (int i = 0; i < copy_count; ++i)
            {
                Graphic::Display *disp =
                    Graphic::g_display_manager->GetDisplay(i);
                if (disp)
                {
                    user_buf[i].id = i;
                    user_buf[i].width = static_cast<uint32_t>(disp->GetWidth());
                    user_buf[i].height =
                        static_cast<uint32_t>(disp->GetHeight());
                    user_buf[i].render_mode =
                        static_cast<uint8_t>(disp->GetRenderMode());
                }
            }
            return static_cast<uint64_t>(count);
        }

        case 31: // SetDisplayMode (レンダーモード設定)
        {
            // arg1: display_id (uint32_t)
            // arg2: mode (uint8_t) - 1=STANDARD, 2=DOUBLE_BUFFER,
            // 3=TRIPLE_BUFFER 戻り値: 0 (成功) または -1 (失敗)
            uint32_t display_id = static_cast<uint32_t>(arg1);
            uint8_t mode = static_cast<uint8_t>(arg2);

            if (!Graphic::g_display_manager)
                return static_cast<uint64_t>(-1);

            Graphic::Display *disp =
                Graphic::g_display_manager->GetDisplay(display_id);
            if (!disp)
                return static_cast<uint64_t>(-1);

            if (mode < 1 || mode > 3)
                return static_cast<uint64_t>(-1);

            if (mode >= 2)
            {
                disp->AllocateBackBuffers(
                    static_cast<Graphic::RenderMode>(mode));
            }
            disp->SetRenderMode(static_cast<Graphic::RenderMode>(mode));
            return 0;
        }

        case 32: // GetSystemInfo (システム情報取得)
        {
            // arg1: SystemInfo* (ユーザー空間のバッファ)
            // 戻り値: 0 (成功)
            struct SystemInfo
            {
                int32_t version_major;
                int32_t version_minor;
                int32_t version_patch;
                int32_t version_revision;
                int32_t build_year;
                int32_t build_month;
                int32_t build_day;
            };

            SystemInfo *user_buf = reinterpret_cast<SystemInfo *>(arg1);
            user_buf->version_major = System::Version.Major;
            user_buf->version_minor = System::Version.Minor;
            user_buf->version_patch = System::Version.Patch;
            user_buf->version_revision = System::Version.Revision;
            user_buf->build_year = System::BuildInfo.Date.Year;
            user_buf->build_month = System::BuildInfo.Date.Month;
            user_buf->build_day = System::BuildInfo.Date.Day;
            return 0;
        }

        default:
            kprintf("Unknown Syscall: %ld\n", syscall_number);
            return 0;
    }
}

void InitializeSyscall()
{
#if defined(__x86_64__)
    // 0. コンテキスト領域の確保
    g_syscall_context = new SyscallContext();

    // 1. EFER.SCE (System Call Enable) ビットを有効化
    // Bit 0: SCE
    const uint64_t kEFER_SCE = 1;
    uint64_t efer = ReadMSR(kMSR_EFER);
    efer |= kEFER_SCE;
    WriteMSR(kMSR_EFER, efer);

    // 2. システムコール専用カーネルスタックの確保 (16KB)
    const size_t kStackSize = 16 * 1024;
    void *stack_mem = MemoryManager::Allocate(kStackSize);

    // スタックは高位アドレスから低位へ伸びるため、末尾をセット
    g_syscall_context->kernel_stack_ptr =
        reinterpret_cast<uint64_t>(stack_mem) + kStackSize;

    // 3. MSRの設定

    // STAR: セグメント設定
    // uint64_t star = (static_cast<uint64_t>(0x08) << 32) |
    // (static_cast<uint64_t>(0x1B) << 48);
    uint64_t star = (static_cast<uint64_t>(0x08) << 32) |
                    (static_cast<uint64_t>(0x18) << 48);
    WriteMSR(kMSR_STAR, star);

    // LSTAR: エントリポイント
    WriteMSR(kMSR_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry));

    // FMASK: RFLAGSマスク
    // 割り込み禁止(IF=0x200)をマスクして、syscall中は割り込み禁止にする
    WriteMSR(kMSR_FMASK, 0x200);

    // GS_BASE設定:
    // カーネルモードでは GS_BASE = g_syscall_context を使う
    // EnterUserModeでのswapgsで: GS_BASE↔KERNEL_GS_BASE
    //   → ユーザー: GS_BASE = 0, KERNEL_GS_BASE = g_syscall_context
    // syscallでのswapgsで: GS_BASE↔KERNEL_GS_BASE
    //   → カーネル: GS_BASE = g_syscall_context, KERNEL_GS_BASE = 0
    // つまり、カーネル状態ではGS_BASEにg_syscall_contextがある必要がある
    const uint32_t kMSR_GS_BASE = 0xC0000101;
    WriteMSR(kMSR_GS_BASE, reinterpret_cast<uint64_t>(g_syscall_context));
    WriteMSR(kMSR_KERNEL_GS_BASE, 0);

    kprintf("[Syscall] Initialized. Context at %lx\n", g_syscall_context);

    uint64_t current_star = ReadMSR(kMSR_STAR);
    kprintf("[Syscall] MSR_STAR set to: %lx\n", current_star);
#else
    kprintf("[Syscall] Not implemented for this architecture.\n");
#endif
}