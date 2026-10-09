// SPDX-License-Identifier: MIT
#pragma once
#include "DmaBuffer.hpp"
namespace n48compute {
struct SysMem {
    n48native::DmaBuffer *md{nullptr}; void *cpu{nullptr}; uint64_t bus{0}, size{0};
    bool valid() const { return md && cpu && bus && size; }
};
struct DmaPool {
    IOService *owner{nullptr}; IOPCIDevice *pci{nullptr}; IOWorkLoop *loop{nullptr};
    n48native::DmaBuffer *buffers[64]{}; uint32_t count{0};
};
bool sysmem_set_pool(DmaPool *pool); // one terminal session per cold boot
kern_return_t sysmem_alloc(SysMem &, uint64_t bytes, uint64_t align = 4096);
void sysmem_free(SysMem &); // published pages never recycled, even on timeout
bool sysmem_iovm_page(uint64_t first, uint32_t page, uint64_t &address);
inline void sysmem_wmb() { __asm__ __volatile__("sfence" ::: "memory"); }
inline void sysmem_rmb() { __asm__ __volatile__("lfence" ::: "memory"); }
}
