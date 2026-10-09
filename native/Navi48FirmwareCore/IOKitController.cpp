// SPDX-License-Identifier: MIT
#include "IOKitController.hpp"
#include <IOKit/IOPlatformExpert.h>
#include <kern/task.h>
#include <pexpert/pexpert.h>

namespace n48native {
namespace {
constexpr uint64_t MiB = 1024 * 1024;
// Deliberately narrow resource profile of the observed target. Reject an
// unreviewed ReBAR profile, rather than confusing PCI aperture with total VRAM.
constexpr uint8_t kRegisters[3] = {0x10, 0x18, 0x24};
constexpr uint64_t kLengths[3] = {256 * MiB, 2 * MiB, 512 * 1024};
constexpr IOOptionBits kMapOptions = kIOMapAnywhere | kIOMapUnique | kIOMapReadOnly | kIOMapInhibitCache;
bool valid(Range r) { return r.bytes && r.offset <= UINT64_MAX - r.bytes; }
bool contains(Range outer, Range inner) {
    return valid(outer) && valid(inner) && inner.offset >= outer.offset &&
        inner.offset - outer.offset <= outer.bytes &&
        inner.bytes <= outer.bytes - (inner.offset - outer.offset);
}
bool overlaps(Range a, Range b) {
    return valid(a) && valid(b) && a.offset < b.offset + b.bytes && b.offset < a.offset + a.bytes;
}
bool sameBar(const BarObservation &a, const BarObservation &b) {
    return a.configRegister == b.configRegister && a.configLow == b.configLow &&
        a.configHigh == b.configHigh && a.cpuPhysical == b.cpuPhysical && a.bytes == b.bytes;
}
bool sameConsole(const ConsoleObservation &a, const ConsoleObservation &b) {
    return a.result == b.result && a.rawBase == b.rawBase && a.rowBytes == b.rowBytes &&
        a.width == b.width && a.height == b.height && a.depth == b.depth &&
        a.offset == b.offset && a.length == b.length;
}
class Guard {
    IOLock *lock_;
public:
    explicit Guard(IOLock *lock) : lock_(lock) { IOLockLock(lock_); }
    ~Guard() { IOLockUnlock(lock_); }
};
} // namespace

IOKitController::IOKitController() : lock_(IOLockAlloc()) {}
IOKitController::~IOKitController() {
    release();
    if (lock_) IOLockFree(lock_);
}
void IOKitController::updateValidity() {
    facts_.observationsValid = facts_.phase == PlatformPhase::MappedUnqualified &&
        facts_.providerOpen && facts_.mappingsHeld;
    facts_.accessEnabled = context_.accessEnabled;
    facts_.blockers = kUnimplementedHardwareProofs;
    if (!facts_.observationsValid) facts_.blockers |= MappingLease;
    if (!facts_.observationsValid || facts_.console.result != ConsoleResult::LocatedInBar0)
        facts_.blockers |= ConsolePlacement;
}
IOKitController::RetiredLease::~RetiredLease() {
    // Foreign callbacks (especially close on an inactive provider) can reenter
    // the controller. It is already terminal/empty and its lock is NOT held.
    for (unsigned i = 3; i-- > 0;) {
        if (windows[i].map) windows[i].map->release();
        if (windows[i].descriptor) windows[i].descriptor->release();
    }
    if (pci) {
        if (providerOpen) pci->close(owner);
        pci->release();
    }
    if (owner) owner->release();
}
void IOKitController::releaseLocked(RetiredLease &retired) {
    // No raw pointer escapes and no hardware-visible operation exists. Thus
    // these resources have NOT been published to the GPU. Do not reuse this
    // cleanup for a future exposed context without actual quiesce/quarantine.
    context_ = {}; // disable and remove every pointer BEFORE dropping its map
    for (unsigned i = 0; i < 3; ++i) {
        retired.windows[i] = windows_[i];
        windows_[i] = {};
    }
    retired.pci = pci_; retired.owner = owner_; retired.providerOpen = facts_.providerOpen;
    pci_ = nullptr; owner_ = nullptr;
    facts_.providerOpen = false; facts_.mappingsHeld = false;
    updateValidity();
}
PlatformResult IOKitController::failLocked(PlatformResult result, RetiredLease &retired) {
    facts_.phase = PlatformPhase::Failed;
    facts_.result = result;
    releaseLocked(retired);
    return result;
}
void IOKitController::release() {
    if (!lock_) return;
    RetiredLease retired; // destroyed AFTER guard, outside the lock
    Guard guard(lock_);
    if (facts_.phase != PlatformPhase::Failed) facts_.phase = PlatformPhase::Released;
    releaseLocked(retired);
}
PlatformSnapshot IOKitController::snapshot() {
    if (!lock_) {
        PlatformSnapshot unavailable{};
        unavailable.result = PlatformResult::NoLock;
        return unavailable;
    }
    Guard guard(lock_);
    return facts_;
}

PlatformResult IOKitController::captureConfig(PlatformSnapshot &out) {
    if (pci_->isInactive() || owner_->isInactive() || owner_->getProvider() != pci_)
        return PlatformResult::ProviderInactive;
    if (!pci_->isOpen(owner_)) return PlatformResult::OpenLost;
    out.identity[0] = pci_->configRead16(uint8_t{0x00});
    out.identity[1] = pci_->configRead16(uint8_t{0x02});
    out.identity[2] = pci_->configRead16(uint8_t{0x2c});
    out.identity[3] = pci_->configRead16(uint8_t{0x2e});
    const uint32_t classRevision = pci_->configRead32(uint8_t{0x08});
    out.identity[4] = classRevision & 0xff;
    out.identity[5] = classRevision >> 8;
    for (unsigned i = 0; i < 6; ++i)
        if (out.identity[i] != kIdentity[i]) return PlatformResult::WrongCard;
    out.command = pci_->configRead16(uint8_t{0x04});
    out.headerType = pci_->configRead8(uint8_t{0x0e});
    // Observe, never enable memory decoding or modify existing bus mastering.
    if (out.command == UINT16_MAX || !(out.command & 2) || (out.headerType & 0x7f) != 0)
        return PlatformResult::InvalidConfig;
    out.bdf = (uint32_t{pci_->getBusNumber()} << 16) | (uint32_t{pci_->getDeviceNumber()} << 11) |
        (uint32_t{pci_->getFunctionNumber()} << 8);
    for (unsigned i = 0; i < 3; ++i) {
        auto &bar = out.bars[i];
        bar.configRegister = kRegisters[i]; bar.bytes = kLengths[i];
        const uint8_t highRegister = bar.configRegister + 4;
        // High/low/high detects a torn 64-bit BAR without any config writes.
        const uint32_t high = i < 2 ? pci_->configRead32(highRegister) : 0;
        bar.configLow = pci_->configRead32(bar.configRegister);
        bar.configHigh = i < 2 ? pci_->configRead32(highRegister) : 0;
        if (high != bar.configHigh) return PlatformResult::ConfigurationChanged;
        const uint32_t expectedFlags = i < 2 ? 0xc : 0; // 64-bit prefetchable, or BAR5 32-bit MMIO
        if ((bar.configLow & 0xf) != expectedFlags) return PlatformResult::InvalidBar;
        bar.cpuPhysical = (uint64_t{bar.configHigh} << 32) | (bar.configLow & ~uint32_t{0xf});
        if (!valid({bar.cpuPhysical, bar.bytes}) || !bar.cpuPhysical || (bar.cpuPhysical & (bar.bytes - 1)))
            return PlatformResult::InvalidBar;
    }
    return PlatformResult::MappingsHeldUnqualified;
}
PlatformResult IOKitController::checkDescriptors() {
    for (unsigned i = 0; i < 3; ++i) {
        facts_.failedBar = kRegisters[i];
        auto *descriptor = windows_[i].descriptor;
        const auto &bar = facts_.bars[i];
        if (!descriptor) return PlatformResult::MissingDescriptor;
        // Ask explicitly for CPU physical segments, NOT mapper/DMA addresses.
        IOByteCount contiguous = 0;
        const uint64_t physical = descriptor->getPhysicalSegment(0, &contiguous, kIOMemoryMapperNone);
        if (descriptor->getLength() != bar.bytes || contiguous != bar.bytes || physical != bar.cpuPhysical)
            return PlatformResult::InvalidDescriptor;
        if (pci_->getDeviceMemoryWithRegister(kRegisters[i]) != descriptor)
            return PlatformResult::ConfigurationChanged;
        for (unsigned j = 0; j < i; ++j)
            if (overlaps({bar.cpuPhysical, bar.bytes}, {facts_.bars[j].cpuPhysical, facts_.bars[j].bytes}))
                return PlatformResult::PhysicalOverlap;
    }
    facts_.failedBar = 0;
    return PlatformResult::MappingsHeldUnqualified;
}
PlatformResult IOKitController::checkMaps() {
    for (unsigned i = 0; i < 3; ++i) {
        facts_.failedBar = kRegisters[i];
        auto &w = windows_[i];
        auto &bar = facts_.bars[i];
        if (!w.map) return PlatformResult::MapFailed;
        IOByteCount contiguous = 0;
        const uint64_t physical = w.map->getPhysicalSegment(0, &contiguous, kIOMemoryMapperNone);
        const IOOptionBits options = w.map->getMapOptions();
        const uint64_t address = w.map->getAddress();
        if (w.map->getMemoryDescriptor() != w.descriptor || w.map->getAddressTask() != kernel_task ||
            w.map->getLength() != bar.bytes || contiguous != bar.bytes || physical != bar.cpuPhysical ||
            (options & kIOMapCacheMask) != kIOMapInhibitCache ||
            (options & (kIOMapUnique | kIOMapReadOnly)) != (kIOMapUnique | kIOMapReadOnly) ||
            !address || (address & 0xfff) || bar.bytes > UINTPTR_MAX - address ||
            (w.address && address != w.address)) return PlatformResult::InvalidMap;
        w.address = address; bar.reportedMapOptions = options;
        for (unsigned j = 0; j < i; ++j)
            if (overlaps({address, bar.bytes}, {windows_[j].address, facts_.bars[j].bytes}))
                return PlatformResult::VirtualOverlap;
    }
    facts_.failedBar = 0;
    return PlatformResult::MappingsHeldUnqualified;
}

ConsoleObservation IOKitController::captureConsole() {
    ConsoleObservation out{};
    auto *platform = IOService::getPlatform();
    PE_Video video{};
    if (!platform || platform->getConsoleInfo(&video) != kIOReturnSuccess) return out;
    out.rawBase = video.v_baseAddr; out.rowBytes = video.v_rowBytes;
    out.width = video.v_width; out.height = video.v_height; out.depth = video.v_depth;
    out.offset = video.v_offset; out.length = video.v_length;
    out.result = ConsoleResult::InvalidGeometry;
    if (!out.width || !out.height || !out.rowBytes || !out.depth || out.depth > 64 || (out.depth & 7) ||
        out.width > UINT64_MAX / (out.depth / 8) || out.width * (out.depth / 8) > out.rowBytes ||
        out.height > UINT64_MAX / out.rowBytes) return out;
    const uint64_t pixels = out.rowBytes * out.height;
    const uint64_t bytes = out.length ? out.length : pixels;
    if (!contains({0, bytes}, {out.offset, pixels})) return out;
    // SDK calls this a base address, but the upstream driver masks low bits as
    // undocumented flags. Do NOT silently normalize them into a placement proof.
    if (!out.rawBase || (out.rawBase & 0xfff)) { out.result = ConsoleResult::AmbiguousBase; return out; }
    out.result = ConsoleResult::OutsideBar0;
    if (!contains({facts_.bars[0].cpuPhysical, facts_.bars[0].bytes}, {out.rawBase, bytes})) return out;
    out.allocationInBar0 = {out.rawBase - facts_.bars[0].cpuPhysical, bytes};
    out.result = ConsoleResult::LocatedInBar0;
    return out;
}

PlatformResult IOKitController::acquire(IOService *owner, IOService *provider, const PlatformRequest &request) {
    if (!lock_) return PlatformResult::NoLock;
    RetiredLease retired; // failure cleanup runs after unlocking
    Guard guard(lock_);
    if (facts_.phase != PlatformPhase::Empty) return PlatformResult::Used;
    uint32_t value = 0;
    if (!PE_parse_boot_argn("navi48-native-platform", &value, sizeof(value)) || value != 1)
        return failLocked(PlatformResult::Disabled, retired);
    if (!owner || owner == provider) return failLocked(PlatformResult::InvalidOwner, retired);
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || owner->getProvider() != pci) return failLocked(PlatformResult::WrongProvider, retired);
    if (owner->isInactive() || pci->isInactive()) return failLocked(PlatformResult::ProviderInactive, retired);
    if (pci->isOpen()) return failLocked(PlatformResult::ProviderBusy, retired);
    owner_ = owner; pci_ = pci; owner_->retain(); pci_->retain();
    if (!pci_->open(owner_, 0, nullptr)) return failLocked(PlatformResult::OpenFailed, retired);
    facts_.providerOpen = true; // IOKit client lease ONLY, never exclusive GPU/VRAM ownership
    auto result = captureConfig(facts_);
    if (result != PlatformResult::MappingsHeldUnqualified) return failLocked(result, retired);
    for (unsigned i = 0; i < 3; ++i) {
        windows_[i].descriptor = pci_->getDeviceMemoryWithRegister(kRegisters[i]);
        if (windows_[i].descriptor) windows_[i].descriptor->retain();
    }
    result = checkDescriptors();
    if (result != PlatformResult::MappingsHeldUnqualified) return failLocked(result, retired);
    const Range aperture{0, facts_.bars[0].bytes};
    if (!request.requiredBytes || request.candidate.bytes < request.requiredBytes ||
        (request.candidate.offset & 0xfff) || (request.candidate.bytes & 0xfff) ||
        !contains(aperture, request.candidate)) return failLocked(PlatformResult::InvalidCandidate, retired);
    facts_.candidateInBar0 = request.candidate;
    facts_.candidateCpuPhysical = {facts_.bars[0].cpuPhysical + request.candidate.offset, request.candidate.bytes};
    facts_.console = captureConsole();
    if (facts_.console.result == ConsoleResult::LocatedInBar0 &&
        overlaps(facts_.console.allocationInBar0, request.candidate)) return failLocked(PlatformResult::ConsoleOverlap, retired);
    for (unsigned i = 0; i < 3; ++i) {
        facts_.failedBar = kRegisters[i];
        // Map the exact descriptor we retained, not a replacement fetched by a
        // second provider lookup. Returned map retains its backing descriptor.
        windows_[i].map = windows_[i].descriptor->map(kMapOptions);
        if (!windows_[i].map) return failLocked(PlatformResult::MapFailed, retired);
    }
    result = revalidateLocked();
    if (result != PlatformResult::MappingsHeldUnqualified) return failLocked(result, retired);
    context_.bar0 = reinterpret_cast<volatile uint8_t *>(windows_[0].address);
    context_.bar0Size = facts_.bars[0].bytes; context_.bar0Phys = facts_.bars[0].cpuPhysical;
    context_.bar2 = reinterpret_cast<volatile uint8_t *>(windows_[1].address);
    context_.bar2Size = facts_.bars[1].bytes; context_.bar2Phys = facts_.bars[1].cpuPhysical;
    context_.rmmio = reinterpret_cast<volatile uint32_t *>(windows_[2].address);
    context_.rmmioSize = facts_.bars[2].bytes;
    // No VRAM size/origin, MC base, reservation, HDP, DMA or teardown proof is
    // invented. Never call bind_access with fabricated Claims to enable this.
    facts_.phase = PlatformPhase::MappedUnqualified;
    facts_.result = PlatformResult::MappingsHeldUnqualified; facts_.mappingsHeld = true;
    updateValidity();
    return facts_.result;
}
PlatformResult IOKitController::revalidateLocked() {
    PlatformSnapshot current{};
    auto result = captureConfig(current);
    if (result != PlatformResult::MappingsHeldUnqualified) return result;
    if (current.command != facts_.command || current.headerType != facts_.headerType || current.bdf != facts_.bdf)
        return PlatformResult::ConfigurationChanged;
    for (unsigned i = 0; i < 3; ++i) {
        facts_.failedBar = kRegisters[i];
        if (!sameBar(current.bars[i], facts_.bars[i])) return PlatformResult::ConfigurationChanged;
    }
    result = checkDescriptors();
    if (result != PlatformResult::MappingsHeldUnqualified) return result;
    result = checkMaps();
    if (result != PlatformResult::MappingsHeldUnqualified) return result;
    if (!sameConsole(captureConsole(), facts_.console)) return PlatformResult::ConsoleChanged;
    // Also check loss of lease/inactivity after the mapping/console calls.
    if (pci_->isInactive() || owner_->isInactive() || owner_->getProvider() != pci_)
        return PlatformResult::ProviderInactive;
    if (!pci_->isOpen(owner_)) return PlatformResult::OpenLost;
    return PlatformResult::MappingsHeldUnqualified;
}
PlatformResult IOKitController::revalidate() {
    if (!lock_) return PlatformResult::NoLock;
    RetiredLease retired;
    Guard guard(lock_);
    if (facts_.phase != PlatformPhase::MappedUnqualified) return PlatformResult::NotMapped;
    const auto result = revalidateLocked();
    return result == PlatformResult::MappingsHeldUnqualified ? result : failLocked(result, retired);
}
} // namespace n48native
