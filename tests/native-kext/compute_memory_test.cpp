// SPDX-License-Identifier: MIT
// Actual native DMA pool on IOKit doubles. NO hardware commands/answers.
#include "ComputeSysMem.hpp"
#include <cstdio>
using namespace n48compute;
static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
int main() {
    auto *pool = new DmaPool; // intentionally reachable terminal publication
    pool->owner = new IOService; pool->pci = new IOPCIDevice; pool->loop = new IOWorkLoop;
    pool->owner->provider = pool->pci; pool->owner->loop = pool->loop; pool->pci->client = pool->owner;
    CHECK(sysmem_set_pool(pool)); CHECK(!sysmem_set_pool(pool));
    const uint64_t initial = fake::dmaBase;
    fake::discontiguous = true;
    SysMem first{};
    CHECK(sysmem_alloc(first, 8192) == kIOReturnSuccess && first.valid());
    CHECK(fake::addressBits == 64 && first.md->snapshot().phase == n48native::DmaBuffer::Phase::Published);
    uint64_t page = 0;
    CHECK(sysmem_iovm_page(first.bus, 0, page) && page == initial);
    CHECK(sysmem_iovm_page(first.bus, 1, page) && page == initial + 8192);
    CHECK(!sysmem_iovm_page(first.bus, 2, page) && page == 0);
    const uint32_t pattern = 0x1234abcd; memcpy(first.cpu, &pattern, 4);
    uint32_t readback = 0;
    CHECK(first.md->read(0, &readback, 4) == kIOReturnSuccess && readback == pattern); // CPU only
    SysMem second{}; fake::dmaBase = initial + 4096;
    CHECK(sysmem_alloc(second, 4096) == kIOReturnSuccess); // hole in sparse first mapping
    CHECK(pool->count == 2);
    SysMem alias{}; fake::dmaBase = initial + 8192;
    CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess && !alias.valid());
    CHECK(pool->count == 2);
    const auto clears = fake::clears, completes = fake::completes;
    sysmem_free(first); CHECK(!first.valid());
    CHECK(pool->count == 2 && fake::clears == clears && fake::completes == completes);
    CHECK(sysmem_iovm_page(initial, 1, page) && page == initial + 8192); // retained
    fake::dmaBase = initial + 0x100000;
    fake::differentDescriptor = true;
    CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess); // no borrow of bounce MD
    fake::differentDescriptor = false;
    CHECK(pool->count == 2);
    for (const uint64_t bytes : {uint64_t{0}, uint64_t{1048577}, UINT64_MAX}) CHECK(sysmem_alloc(alias, bytes) != kIOReturnSuccess);
    CHECK(sysmem_alloc(alias, 4096, 8192) != kIOReturnSuccess);
    CHECK(sysmem_alloc(alias, 4096, 3) != kIOReturnSuccess);
    CHECK(sysmem_alloc(second, 4096) != kIOReturnSuccess); // no stale SysMem reuse
    pool->loop->gated = true; CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess); pool->loop->gated = false;
    pool->pci->client = nullptr; CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess); pool->pci->client = pool->owner;
    fake::dmaBase = (uint64_t{1} << 48); CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess);
    fake::dmaBase = initial + 0x100000;
    const uint32_t oldCount = pool->count; pool->count = 64;
    CHECK(sysmem_alloc(alias, 4096) != kIOReturnSuccess); pool->count = oldCount;
    CHECK(!sysmem_iovm_page(initial + 17, 0, page));
    CHECK(fake::physicalReads == 0);
    std::printf("native_compute_memory: %u checks, %u failed (CPU/IOKit doubles only)\n", checks, failed);
    return failed ? 1 : 0;
}
