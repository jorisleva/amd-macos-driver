// SPDX-License-Identifier: MIT
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>

namespace n48native {
// System RAM for the future GPU page tables/rings/writeback. NOT a VRAM
// reservation, GPU VA, MC address, or proof that a Radeon transfer succeeded.
// All calls externally serialized; allocation/prepare/cleanup MUST run outside
// the owner's workloop gate (IODMACommand::prepare may block).
class DmaBuffer final {
public:
    static constexpr uint64_t kPageBytes = 4096;
    static constexpr uint32_t kMaxPages = 256; // 1 MiB per prototype buffer
    enum class Phase : uint32_t { Empty, Preparing, Prepared, Published, Failed, Released, Quarantined };
    struct Snapshot {
        Phase phase{Phase::Empty};
        IOReturn result{kIOReturnNotReady};
        uint64_t bytes{0};
        uint32_t pages{0};
        bool deviceMapper{false}; // false: IOKit's default mapper, NOT an identity-DMA claim
    };

    DmaBuffer() = default;
    ~DmaBuffer();
    DmaBuffer(const DmaBuffer &) = delete;
    DmaBuffer &operator=(const DmaBuffer &) = delete;
    IOReturn allocate(IOService *owner, IOPCIDevice *pci, IOWorkLoop *loop, uint64_t bytes);
    Snapshot snapshot() const { return facts_; }
    // Addresses are IOVM output from IODMACommand, never getPhysicalSegment().
    // A GART consumer must bind each page, not mistake this for a GPU VA.
    bool pageAddress(uint32_t page, uint64_t &ioVmAddress) const;
    bool contiguousAddress(uint64_t &ioVmAddress) const; // rejects discontiguous IOVM pages
    IOReturn write(uint64_t offset, const void *source, uint64_t bytes);
    IOReturn read(uint64_t offset, void *destination, uint64_t bytes);
    IOReturn syncForDevice();
    IOReturn syncForCpu(); // caller must FIRST wait for actual GPU completion
    // Mark BEFORE inserting a PTE or publishing any address to the GPU.
    bool markPublished();
    // No quiescence producer exists yet: Published buffers are deliberately
    // quarantined until reboot, NEVER freed on timeout, stop or destruction.
    IOReturn release();

private:
    struct Resources {
        IOService *owner{nullptr};
        IOPCIDevice *pci{nullptr};
        IOWorkLoop *loop{nullptr};
        IOMapper *mapper{nullptr};
        IOBufferMemoryDescriptor *memory{nullptr};
        IODMACommand *command{nullptr};
        bool memoryPrepared{false};
        Resources *next{nullptr}; // terminal quarantine, no future reuse
        IODMACommand::Segment64 pages[kMaxPages]{};
    };
    Resources *resources_{nullptr};
    static void *volatile quarantine_;
    Snapshot facts_{};
    bool operating_{false}; // foreign callbacks cannot reenter release/prepare
    bool ready() const;
    bool outsideGate() const;
    bool liveLease() const;
    IOReturn fail(IOReturn result);
    void quarantine();
    IOReturn retire();
};
} // namespace n48native
