// SPDX-License-Identifier: MIT
#include "ComputeSysMem.hpp"
#include <libkern/OSAtomic.h>
namespace n48compute {
static void *volatile activePool = nullptr;
bool sysmem_set_pool(DmaPool *pool) { return pool && OSCompareAndSwapPtr(nullptr, pool, &activePool); }
kern_return_t sysmem_alloc(SysMem &m, uint64_t bytes, uint64_t align) {
    auto *pool = static_cast<DmaPool *>(activePool);
    if (!pool || m.md || !bytes || bytes > 1024 * 1024 || !align || (align & (align - 1)) || align > 4096 || pool->count >= 64)
        return kIOReturnBadArgument;
    bytes = (bytes + 4095) & ~uint64_t{4095};
    auto *buffer = new n48native::DmaBuffer;
    if (!buffer) return kIOReturnNoMemory;
    IOReturn result = buffer->allocate(pool->owner, pool->pci, pool->loop, bytes, true);
    void *cpu = result == kIOReturnSuccess ? buffer->coherentCpuAddress() : nullptr;
    uint64_t first = 0;
    if (!cpu || !buffer->pageAddress(0, first)) { delete buffer; return kIOReturnIOError; }
    // Every PTE below uses the actual per-page IOVM, not first + page*4096.
    // Reject aliases with any previous allocation before exposing a new one.
    for (uint32_t i = 0; i < bytes / 4096; ++i) {
        uint64_t address = 0; if (!buffer->pageAddress(i, address)) { delete buffer; return kIOReturnIOError; }
        for (uint32_t j = 0; j < pool->count; ++j) {
            const auto facts = pool->buffers[j]->snapshot();
            for (uint32_t p = 0; p < facts.pages; ++p) {
                uint64_t other = 0;
                if (!pool->buffers[j]->pageAddress(p, other) || address == other) { delete buffer; return kIOReturnIOError; }
            }
        }
    }
    // Publication is conservative: callers may put bus in PTEs or registers
    // immediately. Never reclaim host pages using an unqualified teardown.
    if (!buffer->markPublished()) { delete buffer; return kIOReturnNotReady; }
    pool->buffers[pool->count++] = buffer;
    m = {buffer, cpu, first, bytes}; return kIOReturnSuccess;
}
void sysmem_free(SysMem &m) { m = {}; } // pool owns all published objects until reboot
bool sysmem_iovm_page(uint64_t first, uint32_t page, uint64_t &address) {
    address = 0; auto *pool = static_cast<DmaPool *>(activePool);
    if (!pool) return false;
    for (uint32_t i = 0; i < pool->count; ++i) {
        uint64_t base = 0;
        if (pool->buffers[i]->pageAddress(0, base) && base == first)
            return pool->buffers[i]->pageAddress(page, address);
    }
    return false;
}
}
