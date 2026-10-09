// SPDX-License-Identifier: MIT
#include "DmaBuffer.hpp"
#include "Preflight.hpp"
#include <IOKit/IOLib.h>
#include <kern/task.h>
#include <libkern/OSAtomic.h>

namespace n48native {
namespace {
struct Operation {
    bool &busy;
    explicit Operation(bool &flag) : busy(flag) { busy = true; }
    ~Operation() { busy = false; }
};
}
void *volatile DmaBuffer::quarantine_ = nullptr;
DmaBuffer::~DmaBuffer() { (void)release(); }
bool DmaBuffer::outsideGate() const {
    return resources_ && resources_->loop && !resources_->loop->inGate();
}
bool DmaBuffer::liveLease() const {
    return resources_ && !resources_->owner->isInactive() && !resources_->pci->isInactive() &&
        resources_->owner->getProvider() == resources_->pci && resources_->pci->isOpen(resources_->owner);
}
bool DmaBuffer::ready() const {
    return resources_ && (facts_.phase == Phase::Prepared || facts_.phase == Phase::Published) &&
        outsideGate() && liveLease();
}
void DmaBuffer::quarantine() {
    auto *held = resources_;
    if (held) {
        // Keep the original +1 references, including the owner/module and mapper.
        // Never complete/unmap an address a GPU may still be using. Global list
        // is terminal; CAS publishes fully initialized nodes, not reusable BOs.
        void *previous;
        do {
            previous = quarantine_;
            held->next = static_cast<Resources *>(previous);
        } while (!OSCompareAndSwapPtr(previous, held, &quarantine_));
        resources_ = nullptr;
    }
    facts_.phase = Phase::Quarantined;
}
IOReturn DmaBuffer::retire() {
    if (!resources_) return kIOReturnSuccess;
    auto *r = resources_;
    if (!outsideGate()) { quarantine(); return kIOReturnNotPermitted; }
    if (r->command) {
        const IOReturn result = r->command->clearMemoryDescriptor(true);
        if (result != kIOReturnSuccess) { quarantine(); return result; }
    }
    if (r->memoryPrepared) {
        const IOReturn result = r->memory->complete(kIODirectionInOut);
        if (result != kIOReturnSuccess) { quarantine(); return result; }
        r->memoryPrepared = false;
    }
    resources_ = nullptr; // no externally reachable state during foreign releases
    if (r->command) r->command->release();
    if (r->memory) r->memory->release();
    if (r->mapper) r->mapper->release();
    r->loop->release(); r->pci->release(); r->owner->release();
    delete r;
    return kIOReturnSuccess;
}
IOReturn DmaBuffer::fail(IOReturn result) {
    facts_.result = result;
    if (facts_.phase == Phase::Published) { quarantine(); return result; }
    facts_.phase = Phase::Failed;
    (void)retire(); // cleanup failure becomes terminal quarantine, never success
    return result;
}
IOReturn DmaBuffer::allocate(IOService *owner, IOPCIDevice *pci, IOWorkLoop *loop, uint64_t bytes) {
    if (operating_ || facts_.phase != Phase::Empty) return kIOReturnNotReady;
    Operation operation(operating_);
    facts_.phase = Phase::Preparing;
    if (!owner || !pci || !loop || owner == pci || owner->getProvider() != pci ||
        owner->getWorkLoop() != loop || loop->inGate() || owner->isInactive() || pci->isInactive() ||
        !pci->isOpen(owner) || !bytes || (bytes & (kPageBytes - 1)) || bytes > kPageBytes * kMaxPages) {
        facts_.phase = Phase::Failed; facts_.result = kIOReturnBadArgument; return facts_.result;
    }
    const uint32_t cr = pci->configRead32(uint8_t{0x08});
    const uint32_t identity[6] = {pci->configRead16(uint8_t{0}), pci->configRead16(uint8_t{2}),
        pci->configRead16(uint8_t{0x2c}), pci->configRead16(uint8_t{0x2e}), cr & 0xff, cr >> 8};
    for (unsigned i = 0; i < 6; ++i) if (identity[i] != kIdentity[i]) {
        facts_.phase = Phase::Failed; facts_.result = kIOReturnBadArgument; return facts_.result;
    }
    resources_ = new Resources;
    if (!resources_) { facts_.phase = Phase::Failed; facts_.result = kIOReturnNoMemory; return facts_.result; }
    auto *r = resources_;
    r->owner = owner; r->pci = pci; r->loop = loop;
    owner->retain(); pci->retain(); loop->retain();
    facts_.bytes = bytes; facts_.pages = static_cast<uint32_t>(bytes / kPageBytes);
    // No physically-contiguous request, kIOMemoryMapperNone or physical mask.
    // IODMACommand, not CPU physical arithmetic, produces the bus page list.
    r->memory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut,
        static_cast<vm_size_t>(bytes), static_cast<vm_offset_t>(kPageBytes));
    if (!r->memory) return fail(kIOReturnNoMemory);
    if (r->memory->getLength() != bytes || !r->memory->getBytesNoCopy()) return fail(kIOReturnIOError);
    IOReturn result = r->memory->prepare(kIODirectionInOut);
    if (result != kIOReturnSuccess) return fail(result);
    r->memoryPrepared = true;
    bzero(r->memory->getBytesNoCopy(), static_cast<size_t>(bytes));
    // copyMapperForDevice returns a retained mapper when the device has one.
    // nullptr delegates to IOKit's documented default mapper in withSpecification;
    // it is NEVER converted into a manually asserted identity-DMA qualification.
    r->mapper = IOMapper::copyMapperForDevice(pci);
    facts_.deviceMapper = r->mapper != nullptr;
    r->command = IODMACommand::withSpecification(kIODMACommandOutputHost64, 48,
        kPageBytes, IODMACommand::kMapped, 0, static_cast<UInt32>(kPageBytes), r->mapper);
    if (!r->command) return fail(kIOReturnNoMemory);
    result = r->command->setMemoryDescriptor(r->memory, false);
    if (result != kIOReturnSuccess) return fail(result);
    result = r->command->prepare(0, bytes);
    if (result != kIOReturnSuccess) return fail(result);
    UInt64 preparedOffset = UINT64_MAX, preparedBytes = 0;
    result = r->command->getPreparedOffsetAndLength(&preparedOffset, &preparedBytes);
    if (result != kIOReturnSuccess) return fail(result);
    if (preparedOffset || preparedBytes != bytes || r->command->getMemoryDescriptor() != r->memory)
        return fail(kIOReturnIOError);
    UInt64 cursor = 0;
    UInt32 count = facts_.pages;
    result = r->command->gen64IOVMSegments(&cursor, r->pages, &count);
    if (result != kIOReturnSuccess) return fail(result);
    if (cursor != bytes || count != facts_.pages) return fail(kIOReturnIOError);
    constexpr uint64_t limit = uint64_t{1} << 48;
    for (UInt32 i = 0; i < count; ++i) {
        const auto &page = r->pages[i];
        if (!page.fIOVMAddr || (page.fIOVMAddr & (kPageBytes - 1)) || page.fLength != kPageBytes ||
            page.fIOVMAddr >= limit || kPageBytes > limit - page.fIOVMAddr) return fail(kIOReturnIOError);
        for (UInt32 j = 0; j < i; ++j)
            if (r->pages[j].fIOVMAddr == page.fIOVMAddr) return fail(kIOReturnIOError);
    }
    if (!liveLease()) return fail(kIOReturnNotReady);
    facts_.phase = Phase::Prepared; facts_.result = kIOReturnSuccess;
    return kIOReturnSuccess;
}
bool DmaBuffer::pageAddress(uint32_t page, uint64_t &ioVmAddress) const {
    ioVmAddress = 0;
    if (operating_ || !ready() || page >= facts_.pages) return false;
    ioVmAddress = resources_->pages[page].fIOVMAddr;
    return true;
}
bool DmaBuffer::contiguousAddress(uint64_t &ioVmAddress) const {
    ioVmAddress = 0;
    if (operating_ || !ready()) return false;
    const uint64_t first = resources_->pages[0].fIOVMAddr;
    for (uint32_t i = 1; i < facts_.pages; ++i)
        if (resources_->pages[i].fIOVMAddr != first + uint64_t{i} * kPageBytes) return false;
    ioVmAddress = first;
    return true;
}
namespace {
bool transferRange(uint64_t allocation, uint64_t offset, uint64_t bytes, const void *buffer) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(buffer);
    return buffer && bytes && bytes <= UINTPTR_MAX - address && offset <= allocation && bytes <= allocation - offset;
}
}
IOReturn DmaBuffer::write(uint64_t offset, const void *source, uint64_t bytes) {
    if (operating_ || !ready()) return kIOReturnNotReady;
    if (!transferRange(facts_.bytes, offset, bytes, source)) return kIOReturnBadArgument;
    Operation operation(operating_);
    // Write the ORIGINAL descriptor, then syncForDevice copies it to any bounce
    // buffer. Writing only the DMA copy would be undone by synchronize(Out).
    return resources_->memory->writeBytes(offset, source, bytes) == bytes ? kIOReturnSuccess : fail(kIOReturnIOError);
}
IOReturn DmaBuffer::read(uint64_t offset, void *destination, uint64_t bytes) {
    if (operating_ || !ready()) return kIOReturnNotReady;
    if (!transferRange(facts_.bytes, offset, bytes, destination)) return kIOReturnBadArgument;
    Operation operation(operating_);
    // Caller waited for a real fence and used syncForCpu to copy a possible
    // bounce buffer back into this original descriptor before reading it.
    return resources_->memory->readBytes(offset, destination, bytes) == bytes ? kIOReturnSuccess : fail(kIOReturnIOError);
}
IOReturn DmaBuffer::syncForDevice() {
    if (operating_ || !ready()) return kIOReturnNotReady;
    Operation operation(operating_);
    const IOReturn result = resources_->command->synchronize(kIODirectionOut);
    return result == kIOReturnSuccess ? result : fail(result);
}
IOReturn DmaBuffer::syncForCpu() {
    if (operating_ || !ready()) return kIOReturnNotReady;
    Operation operation(operating_);
    const IOReturn result = resources_->command->synchronize(kIODirectionIn);
    return result == kIOReturnSuccess ? result : fail(result);
}
bool DmaBuffer::markPublished() {
    if (operating_ || facts_.phase != Phase::Prepared || !ready()) return false;
    facts_.phase = Phase::Published;
    return true;
}
IOReturn DmaBuffer::release() {
    if (operating_) return kIOReturnNotReady;
    Operation operation(operating_);
    if (facts_.phase == Phase::Quarantined) return kIOReturnNotReady;
    if (facts_.phase == Phase::Published) { quarantine(); return kIOReturnNotReady; }
    const IOReturn result = retire();
    if (result != kIOReturnSuccess) { facts_.result = result; return result; }
    if (facts_.phase != Phase::Failed) facts_.phase = Phase::Released;
    return kIOReturnSuccess;
}
} // namespace n48native
