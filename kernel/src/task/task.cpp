#include "../heap.hpp"
#include "../kernel_runtime.hpp"
#include "task.hpp"

void *Task::operator new(size_t size)
{
    // 1. アライメントに必要な追加領域を計算
    //    - 15バイト: アライメントの余白
    //    - ポインタサイズ: 元のアドレスを保存するため
    const size_t alloc_size = size + 15 + sizeof(void *);

    // 2. ヒープからアライメントを考慮せず、大きめにメモリを確保する
    //    (kmallocの第2引数は渡さない、またはデフォルト値に任せる)
    void *p = heap::kmalloc(alloc_size);

    // 3. 確保した領域の中から、16バイトにアライメントされたアドレスを探す
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    uintptr_t aligned_addr = (addr + 15 + sizeof(void *)) & ~0xFULL;

    // 4. アライメントされたアドレスの直前に、確保した元のポインタ(p)を保存する
    //    (後のdeleteで、この元のポインタを解放する必要があるため)
    void **original_p_ptr = reinterpret_cast<void **>(aligned_addr - sizeof(void *));
    *original_p_ptr = p;

    // 5. アライメントされたアドレスを返す
    return reinterpret_cast<void *>(aligned_addr);
}

/**
 * @brief Taskクラス専用のoperator delete。
 * operator new で確保したメモリを正しく解放する。
 */
void Task::operator delete(void *ptr)
{
    // 1. 引数で渡されたポインタ(ptr)の直前を参照し、保存しておいた元のポインタを取り出す
    void **original_p_ptr = reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(ptr) - sizeof(void *));
    void *original_p = *original_p_ptr;

    // 2. 元のポインタを使って、ヒープ領域を解放する
    heap::kfree(original_p);
}

Task::Task(TaskId id, uint64_t entry_point)
    : id_(id), state_(TaskState::READY)
{
    const size_t stack_size = 32 * 1024; // 8KB
    raw_stack_buffer_ = new uint8_t[stack_size];

    // スタックの最上位アドレスを計算
    uintptr_t stack_top = reinterpret_cast<uintptr_t>(raw_stack_buffer_) + stack_size;

    // Context構造体を初期化
    memset(&context_, 0, sizeof(Context));

    // RSPをスタックの最上位に設定
    context_.rsp = stack_top;

    // その他のレジスタを設定
    context_.rip = entry_point;
    context_.rflags = 0x202; // IF=1
    asm volatile("mov %%cr3, %0" : "=r"(context_.cr3));
    asm volatile("mov %%cs, %0" : "=r"(context_.cs));
    asm volatile("mov %%ss, %0" : "=r"(context_.ss));
}

Task::~Task()
{
    delete[] raw_stack_buffer_;
}