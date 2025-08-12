#include <new>
#include "heap.hpp"

void *operator new(size_t n) { return heap::kmalloc(n, 16, false); }
void *operator new[](size_t n) { return heap::kmalloc(n, 16, false); }
void operator delete(void *p) noexcept { heap::kfree(p); }
void operator delete[](void *p) noexcept { heap::kfree(p); }
void operator delete(void *p, size_t) noexcept { heap::kfree(p); }
void operator delete[](void *p, size_t) noexcept { heap::kfree(p); }
