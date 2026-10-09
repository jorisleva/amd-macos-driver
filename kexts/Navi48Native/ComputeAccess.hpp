// SPDX-License-Identifier: MIT
// Experimental compute backend, compiled with -Damdgpu=n48compute. It does NOT
// alter the old RO controller's Claims/blockers or share its DeviceContext ABI.
#pragma once
#include <stdint.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOReturn.h>
#include "amdgpu_ip.h"
#include "NativeLog.hpp"
#define REG_SET_FIELD(value, reg, field, val) \
    ((((uint32_t)(value)) & ~((uint32_t)(reg##__##field##_MASK))) | \
     ((((uint32_t)(val)) << ((uint32_t)(reg##__##field##__SHIFT))) & ((uint32_t)(reg##__##field##_MASK))))
#define REG_GET_FIELD(value, reg, field) \
    ((((uint32_t)(value)) & ((uint32_t)(reg##__##field##_MASK))) >> ((uint32_t)(reg##__##field##__SHIFT)))
namespace amdgpu {
struct DeviceContext {
    volatile uint32_t *rmmio{nullptr}; size_t rmmioSize{0};
    volatile uint8_t *bar0{nullptr}; size_t bar0Size{0}; uint64_t bar0Phys{0};
    volatile uint8_t *bar2{nullptr}; size_t bar2Size{0}; uint64_t bar2Phys{0};
    uint64_t vramSizeBytes{0}, vramMcBase{0}, vramBase{0}, vramLimit{0};
    IPBaseTable ip;
    bool psoCAlive{false}, smuOnline{false}, gmcReady{false};
    DoorbellState doorbell{};
    bool trialEnabled{false}, hdpConfigured{false}; // permissions, NOT qualification
    mutable IOReturn fault{kIOReturnSuccess};
    IOLock *indirectLock{nullptr};
    void *lease{nullptr}; bool (*leaseAlive)(void *){nullptr};
    uint64_t vramMC(uint64_t offset) const;
};
inline bool refuse(const DeviceContext &d, IOReturn error) {
    if (d.fault == kIOReturnSuccess) d.fault = error;
    return false;
}
inline bool ready(const DeviceContext &d) {
    return d.fault == kIOReturnSuccess && d.trialEnabled && d.leaseAlive && d.leaseAlive(d.lease);
}
// Same error-propagation surface as the audited native PSP patch, but these
// helpers guard ONLY this explicitly permitted experiment, not old Claims.
enum class AccessError { Alignment, McAddress, Timeout };
enum class AccessOp { Address, Poll };
inline IOReturn access_status(const DeviceContext &d) {
    return ready(d) ? kIOReturnSuccess : (d.fault != kIOReturnSuccess ? d.fault : kIOReturnNotReady);
}
inline void access_fail(const DeviceContext &d, AccessError e, AccessOp, uint64_t = 0, uint64_t = 0) {
    refuse(d, e == AccessError::Timeout ? kIOReturnTimeout : kIOReturnBadArgument);
}
inline bool access_source(const DeviceContext &d, const void *src, uint64_t bytes) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(src);
    return (ready(d) && src && bytes && bytes <= UINTPTR_MAX - address) || refuse(d, kIOReturnBadArgument);
}
inline bool hdp_flush_ready(const DeviceContext &d) {
    return (ready(d) && d.hdpConfigured) || refuse(d, kIOReturnNotReady);
}
inline bool span(uint64_t size, uint64_t offset, uint64_t bytes) {
    return bytes && offset <= size && bytes <= size - offset;
}
inline bool regSpan(const DeviceContext &d, uint32_t reg) {
    return (ready(d) && d.rmmio && span(d.rmmioSize, uint64_t{reg} * 4, 4)) || refuse(d, kIOReturnNotReady);
}
inline bool vramSpan(const DeviceContext &d, uint64_t offset, uint64_t bytes, uint64_t align = 4) {
    return (ready(d) && d.bar0 && !(offset & (align - 1)) && !(bytes & (align - 1)) &&
        d.vramSizeBytes && d.vramBase < d.vramLimit && d.vramLimit <= d.bar0Size &&
        d.vramLimit <= d.vramSizeBytes && offset >= d.vramBase && span(d.vramLimit, offset, bytes)) ||
        refuse(d, kIOReturnBadArgument);
}
inline bool access_vram(const DeviceContext &d, uint64_t offset, uint64_t bytes) { return vramSpan(d, offset, bytes); }
inline uint64_t DeviceContext::vramMC(uint64_t offset) const {
    if (!vramSpan(*this, offset, 1, 1) || !vramMcBase || vramSizeBytes > UINT64_MAX - vramMcBase) {
        refuse(*this, kIOReturnBadArgument); return UINT64_MAX;
    }
    return vramMcBase + offset;
}
inline uint32_t SOC15_REG_OFFSET_BIDX(const DeviceContext &d, IPBlock block, int idx, uint32_t reg) {
    if (!ready(d) || static_cast<unsigned>(block) >= static_cast<unsigned>(IPBlock::Count) ||
        idx < 0 || idx >= IPBaseTable::kMaxBaseSegments) { refuse(d, kIOReturnBadArgument); return UINT32_MAX; }
    uint32_t base = d.ip.getBase(block, idx);
    if (base == UINT32_MAX || reg > UINT32_MAX - base) { refuse(d, kIOReturnBadArgument); return UINT32_MAX; }
    return base + reg; // discovery can legitimately report a zero NBIO segment
}
inline uint32_t SOC15_REG_OFFSET(const DeviceContext &d, IPBlock block, uint32_t reg) {
    return SOC15_REG_OFFSET_BIDX(d, block, 0, reg);
}
inline uint32_t RREG32(const DeviceContext &d, uint32_t reg) { return regSpan(d, reg) ? d.rmmio[reg] : UINT32_MAX; }
inline void WREG32(const DeviceContext &d, uint32_t reg, uint32_t value) { if (regSpan(d, reg)) d.rmmio[reg] = value; }
inline uint32_t RREG32_abs(const DeviceContext &d, uint32_t reg) { return RREG32(d, reg); }
inline void storeFence() { __asm__ __volatile__("sfence" ::: "memory"); }
inline uint32_t RBAR0_32(const DeviceContext &d, uint64_t off) {
    return vramSpan(d, off, 4) ? *reinterpret_cast<const volatile uint32_t *>(d.bar0 + off) : UINT32_MAX;
}
inline uint32_t RBAR2_32(const DeviceContext &d, uint64_t off) { return RBAR0_32(d, off); }
inline uint64_t RBAR0_64(const DeviceContext &d, uint64_t off) {
    return vramSpan(d, off, 8, 8) ? *reinterpret_cast<const volatile uint64_t *>(d.bar0 + off) : UINT64_MAX;
}
inline void WBAR0_32(const DeviceContext &d, uint64_t off, uint32_t value) {
    if (!vramSpan(d, off, 4)) return;
    *reinterpret_cast<volatile uint32_t *>(d.bar0 + off) = value; storeFence();
}
inline void WBAR0_64(const DeviceContext &d, uint64_t off, uint64_t value) {
    if (!vramSpan(d, off, 8, 8)) return;
    *reinterpret_cast<volatile uint64_t *>(d.bar0 + off) = value; storeFence();
}
inline void bar0_memcpy_to_vram(const DeviceContext &d, uint64_t off, const void *src, uint64_t bytes) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(src);
    if (!vramSpan(d, off, bytes) || !src || bytes > UINTPTR_MAX - address) { refuse(d, kIOReturnBadArgument); return; }
    const auto *p = static_cast<const uint8_t *>(src);
    for (uint64_t i = 0; i < bytes; i += 4) {
        uint32_t word; memcpy(&word, p + i, 4);
        *reinterpret_cast<volatile uint32_t *>(d.bar0 + off + i) = word;
    }
    storeFence();
}
inline void bar0_memset_vram(const DeviceContext &d, uint64_t off, uint32_t value, uint64_t bytes) {
    if (!vramSpan(d, off, bytes)) return;
    for (uint64_t i = 0; i < bytes; i += 4) *reinterpret_cast<volatile uint32_t *>(d.bar0 + off + i) = value;
    storeFence();
}
inline void vram_memcpy(const DeviceContext &d, uint64_t off, const void *src, size_t bytes) {
    bar0_memcpy_to_vram(d, off, src, bytes); // NO fallback outside the explicit scratch
}
// MM window reads may also survey the top-of-VRAM discovery/runtime database.
// Writes stay confined to scratch. All compound index/data operations serialize.
inline uint32_t RVRAM32_via_mm(const DeviceContext &d, uint64_t off) {
    if (!ready(d) || !d.indirectLock || (off & 3) || !span(d.vramSizeBytes, off, 4) ||
        !regSpan(d, 0) || !regSpan(d, 1) || !regSpan(d, 6)) { refuse(d, kIOReturnBadArgument); return UINT32_MAX; }
    IOLockLock(d.indirectLock);
    WREG32(d, 6, static_cast<uint32_t>(off >> 31));
    WREG32(d, 0, (static_cast<uint32_t>(off) & 0x7ffffffcu) | 0x80000000u);
    uint32_t value = RREG32(d, 1);
    IOLockUnlock(d.indirectLock); return value;
}
inline void WVRAM32_via_mm(const DeviceContext &d, uint64_t off, uint32_t value) {
    if (!vramSpan(d, off, 4) || !d.indirectLock || !regSpan(d, 0) || !regSpan(d, 1) || !regSpan(d, 6)) return;
    IOLockLock(d.indirectLock);
    WREG32(d, 6, static_cast<uint32_t>(off >> 31));
    WREG32(d, 0, (static_cast<uint32_t>(off) & 0x7ffffffcu) | 0x80000000u);
    WREG32(d, 1, value); IOLockUnlock(d.indirectLock);
}
inline uint32_t SMN_RREG32(const DeviceContext &d, uint32_t reg) {
    if (!d.indirectLock || reg > UINT32_MAX / 4 || !regSpan(d, NBIORegs::BIF_BX1_PCIE_INDEX2) ||
        !regSpan(d, NBIORegs::BIF_BX1_PCIE_DATA2)) { refuse(d, kIOReturnBadArgument); return UINT32_MAX; }
    IOLockLock(d.indirectLock); WREG32(d, NBIORegs::BIF_BX1_PCIE_INDEX2, reg * 4);
    (void)RREG32(d, NBIORegs::BIF_BX1_PCIE_INDEX2);
    uint32_t value = RREG32(d, NBIORegs::BIF_BX1_PCIE_DATA2); IOLockUnlock(d.indirectLock); return value;
}
inline void SMN_WREG32(const DeviceContext &d, uint32_t reg, uint32_t value) {
    if (!d.indirectLock || reg > UINT32_MAX / 4 || !regSpan(d, NBIORegs::BIF_BX1_PCIE_INDEX2) ||
        !regSpan(d, NBIORegs::BIF_BX1_PCIE_DATA2)) { refuse(d, kIOReturnBadArgument); return; }
    IOLockLock(d.indirectLock); WREG32(d, NBIORegs::BIF_BX1_PCIE_INDEX2, reg * 4);
    (void)RREG32(d, NBIORegs::BIF_BX1_PCIE_INDEX2);
    WREG32(d, NBIORegs::BIF_BX1_PCIE_DATA2, value); IOLockUnlock(d.indirectLock);
}
inline uint32_t PCIE_PORT_RREG32(const DeviceContext &d, uint32_t reg) {
    const uint32_t idx = SOC15_REG_OFFSET_BIDX(d, IPBlock::NBIO, 1, NBIORegs::BIF_BX_PF1_RSMU_INDEX);
    const uint32_t dat = SOC15_REG_OFFSET_BIDX(d, IPBlock::NBIO, 1, NBIORegs::BIF_BX_PF1_RSMU_DATA);
    if (!d.indirectLock || reg > UINT32_MAX / 4 || !regSpan(d, idx) || !regSpan(d, dat)) return UINT32_MAX;
    IOLockLock(d.indirectLock); WREG32(d, idx, reg * 4); (void)RREG32(d, idx);
    uint32_t value = RREG32(d, dat); IOLockUnlock(d.indirectLock); return value;
}
inline void PCIE_PORT_WREG32(const DeviceContext &d, uint32_t reg, uint32_t value) {
    const uint32_t idx = SOC15_REG_OFFSET_BIDX(d, IPBlock::NBIO, 1, NBIORegs::BIF_BX_PF1_RSMU_INDEX);
    const uint32_t dat = SOC15_REG_OFFSET_BIDX(d, IPBlock::NBIO, 1, NBIORegs::BIF_BX_PF1_RSMU_DATA);
    if (!d.indirectLock || reg > UINT32_MAX / 4 || !regSpan(d, idx) || !regSpan(d, dat)) return;
    IOLockLock(d.indirectLock); WREG32(d, idx, reg * 4); (void)RREG32(d, idx);
    WREG32(d, dat, value); IOLockUnlock(d.indirectLock);
}
inline void amdgpu_hdp_flush(const DeviceContext &d) {
    if (!d.hdpConfigured) { refuse(d, kIOReturnNotReady); return; }
    storeFence(); WREG32(d, 0x7f000 / 4, 0); (void)RREG32(d, 0x7f000 / 4);
}
inline bool doorbellSpan(const DeviceContext &d, uint64_t off, uint64_t bytes) {
    return (ready(d) && d.bar2 && !(off & (bytes - 1)) && span(d.bar2Size, off, bytes)) || refuse(d, kIOReturnBadArgument);
}
inline void WDOORBELL32(const DeviceContext &d, uint64_t off, uint32_t value) {
    if (!doorbellSpan(d, off, 4)) return;
    *reinterpret_cast<volatile uint32_t *>(d.bar2 + off) = value; storeFence();
}
inline void WDOORBELL64(const DeviceContext &d, uint64_t off, uint64_t value) {
    if (!doorbellSpan(d, off, 8)) return;
    *reinterpret_cast<volatile uint64_t *>(d.bar2 + off) = value; storeFence();
}
inline uint32_t RDOORBELL32(const DeviceContext &d, uint64_t off) {
    return doorbellSpan(d, off, 4) ? *reinterpret_cast<const volatile uint32_t *>(d.bar2 + off) : UINT32_MAX;
}
inline kern_return_t poll_psp_response(const DeviceContext &d, uint32_t reg, uint32_t mask,
                                      uint32_t expected, uint64_t timeout, uint32_t *out) {
    if (out) *out = 0;
    if (!mask || (expected & ~mask) || timeout > 20000000) return kIOReturnBadArgument;
    for (uint64_t elapsed = 0;; elapsed += 1000) {
        uint32_t value = RREG32(d, reg); if (!ready(d)) return kIOReturnNotReady;
        if (out) *out = value;
        if (value == UINT32_MAX) return kIOReturnIOError;
        if ((value & 0x80000000u) && (value & 0xffffu)) return kIOReturnIOError;
        if ((value & mask) == expected) return kIOReturnSuccess;
        if (elapsed >= timeout) return kIOReturnTimeout;
        IOSleep(1);
    }
}
inline bool poll_reg(const DeviceContext &d, uint32_t reg, uint32_t mask, uint32_t expected,
                     uint64_t timeout, uint32_t *out = nullptr) {
    if (!mask || (expected & ~mask) || timeout > 20000000) return false;
    for (uint64_t elapsed = 0;; elapsed += 1000) {
        uint32_t value = RREG32(d, reg); if (!ready(d) || value == UINT32_MAX) return false;
        if (out) *out = value;
        if ((value & mask) == expected) return true;
        if (elapsed >= timeout) return false;
        IOSleep(1);
    }
}
} // namespace amdgpu (renamed by the compute build only)
