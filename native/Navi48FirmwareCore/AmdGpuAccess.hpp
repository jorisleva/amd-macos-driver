// SPDX-License-Identifier: MIT
// Local replacement for Navi48's amd/amdgpu_regs.h. Derived interface from
// Navi48-MacOS (MIT) / mac-amdgpu (MIT); upstream notices retained by the builder.
// Serialized use only. Mappings must remain valid for the whole session.
#pragma once
#include <stdint.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOReturn.h>
#include "amd/amdgpu_ip.h"
#include "NativeLog.hpp"

#define REG_SET_FIELD(value, reg, field, val) \
    ((((uint32_t)(value)) & ~((uint32_t)(reg##__##field##_MASK))) | \
     ((((uint32_t)(val)) << ((uint32_t)(reg##__##field##__SHIFT))) & \
      ((uint32_t)(reg##__##field##_MASK))))
#define REG_GET_FIELD(value, reg, field) \
    ((((uint32_t)(value)) & ((uint32_t)(reg##__##field##_MASK))) >> ((uint32_t)(reg##__##field##__SHIFT)))

extern "C" void *memcpy(void *, const void *, size_t);

namespace amdgpu {
enum class AccessError : uint32_t {
    None, Disabled, NullMapping, Range, Alignment, Source, IpBase,
    McAddress, UnsupportedIndirect, UnqualifiedFlush, PollArgument, Timeout
};
enum class AccessOp : uint32_t { Bind, Register, Vram, Doorbell, Copy, Fill, Address, Indirect, Flush, Poll };
struct AccessFault {
    AccessError error{AccessError::None};
    AccessOp operation{AccessOp::Bind};
    uint64_t offset{0}, bytes{0};
};

struct DeviceContext {
    volatile uint32_t *rmmio{nullptr};
    size_t rmmioSize{0};
    volatile uint8_t *bar0{nullptr};
    size_t bar0Size{0};
    uint64_t bar0Phys{0};
    volatile uint8_t *bar2{nullptr};
    size_t bar2Size{0};
    uint64_t bar2Phys{0};
    uint64_t vramSizeBytes{0}, vramMcBase{0};
    uint64_t vramBase{0}, vramLimit{0}; // owned half-open interval, NOT all visible VRAM
    IPBaseTable ip;
    bool psoCAlive{false}, smuOnline{false}, gmcReady{false};
    DoorbellState doorbell{};
    bool accessEnabled{false}; // set only by the explicit mapping binder
    mutable AccessFault accessFault{}; // first failure, sticky; no reset/rearm API
    // Must be established by the future platform adapter, not guessed/skipped.
    bool hdpFlushQualified{false};
    uint32_t hdpFlushDword{0}, hdpReadbackDword{0};
    uint64_t vramMC(uint64_t offset) const;
};

inline bool access_fail(const DeviceContext &c, AccessError error, AccessOp op,
                        uint64_t offset = 0, uint64_t bytes = 0) {
    if (c.accessFault.error == AccessError::None) {
        c.accessFault = {error, op, offset, bytes};
        n48_logf("Navi48FirmwareCore: access refused error=%u op=%u offset=0x%llx bytes=0x%llx\n",
                 static_cast<unsigned>(error), static_cast<unsigned>(op),
                 static_cast<unsigned long long>(offset), static_cast<unsigned long long>(bytes));
    }
    return false;
}
inline kern_return_t access_status(const DeviceContext &c) {
    switch (c.accessFault.error) {
    case AccessError::None: return c.accessEnabled ? kIOReturnSuccess : kIOReturnNotReady;
    case AccessError::Disabled: return kIOReturnNotReady;
    case AccessError::UnsupportedIndirect:
    case AccessError::UnqualifiedFlush: return kIOReturnUnsupported;
    case AccessError::Timeout: return kIOReturnTimeout;
    default: return kIOReturnIOError;
    }
}
inline bool access_ready(const DeviceContext &c, AccessOp op) {
    if (c.accessFault.error != AccessError::None) return false;
    return c.accessEnabled || access_fail(c, AccessError::Disabled, op);
}
inline bool contains_bytes(uint64_t size, uint64_t offset, uint64_t bytes) {
    return bytes != 0 && offset <= size && bytes <= size - offset;
}
inline bool window_shape(const volatile void *base, uint64_t size, uint64_t alignment) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(base);
    return base && size && !(address & (alignment - 1)) && size <= UINTPTR_MAX - address;
}
inline bool access_window(const DeviceContext &c, const volatile void *base, uint64_t size,
                          uint64_t offset, uint64_t bytes, uint64_t alignment, AccessOp op) {
    if (!access_ready(c, op)) return false;
    if (!base) return access_fail(c, AccessError::NullMapping, op, offset, bytes);
    if ((reinterpret_cast<uintptr_t>(base) & (alignment - 1)) || (offset & (alignment - 1)) ||
        (bytes & (alignment - 1))) return access_fail(c, AccessError::Alignment, op, offset, bytes);
    if (!window_shape(base, size, alignment) || !contains_bytes(size, offset, bytes))
        return access_fail(c, AccessError::Range, op, offset, bytes);
    return true;
}
inline bool access_vram(const DeviceContext &c, uint64_t offset, uint64_t bytes,
                        uint64_t alignment = 4, AccessOp op = AccessOp::Vram) {
    if (!access_window(c, c.bar0, c.bar0Size, offset, bytes, alignment, op)) return false;
    if (!c.vramSizeBytes || c.vramLimit > c.vramSizeBytes || c.vramLimit > c.bar0Size ||
        c.vramBase >= c.vramLimit || offset < c.vramBase ||
        !contains_bytes(c.vramLimit, offset, bytes))
        return access_fail(c, AccessError::Range, op, offset, bytes);
    return true;
}
inline uint64_t DeviceContext::vramMC(uint64_t offset) const {
    if (!access_vram(*this, offset, 1, 1, AccessOp::Address)) return UINT64_MAX;
    if (!vramMcBase || vramSizeBytes > UINT64_MAX - vramMcBase) {
        access_fail(*this, AccessError::McAddress, AccessOp::Address, offset, 1);
        return UINT64_MAX;
    }
    return vramMcBase + offset;
}

inline uint32_t SOC15_REG_OFFSET_BIDX(const DeviceContext &c, IPBlock block, int index, uint32_t reg) {
    if (!access_ready(c, AccessOp::Address)) return UINT32_MAX;
    if (static_cast<unsigned>(block) >= static_cast<unsigned>(IPBlock::Count) ||
        index < 0 || index >= IPBaseTable::kMaxBaseSegments) {
        access_fail(c, AccessError::IpBase, AccessOp::Address, reg, 4);
        return UINT32_MAX;
    }
    const uint32_t base = c.ip.getBase(block, index);
    // Zero remains unresolved until an explicit zero-base discovery contract exists.
    if (!base || base == UINT32_MAX || reg > UINT32_MAX - base) {
        access_fail(c, AccessError::IpBase, AccessOp::Address, reg, 4);
        return UINT32_MAX;
    }
    return base + reg;
}
inline uint32_t SOC15_REG_OFFSET(const DeviceContext &c, IPBlock block, uint32_t reg) {
    return SOC15_REG_OFFSET_BIDX(c, block, 0, reg);
}
inline bool register_span(const DeviceContext &c, uint32_t reg) {
    return access_window(c, c.rmmio, c.rmmioSize, uint64_t{reg} * 4, 4, 4, AccessOp::Register);
}
inline uint32_t RREG32(const DeviceContext &c, uint32_t reg) {
    return register_span(c, reg) ? c.rmmio[reg] : UINT32_MAX;
}
inline void WREG32(const DeviceContext &c, uint32_t reg, uint32_t value) {
    if (register_span(c, reg)) c.rmmio[reg] = value;
}
inline uint32_t RREG32_abs(const DeviceContext &c, uint32_t reg) { return RREG32(c, reg); }
inline void storeFence() { __asm__ __volatile__("sfence" ::: "memory"); }
inline uint32_t RBAR0_32(const DeviceContext &c, uint64_t offset) {
    return access_vram(c, offset, 4) ? *reinterpret_cast<const volatile uint32_t *>(c.bar0 + offset) : UINT32_MAX;
}
// Historical alias: this is VRAM in BAR0, NOT the BAR2 doorbell aperture.
inline uint32_t RBAR2_32(const DeviceContext &c, uint64_t offset) { return RBAR0_32(c, offset); }
inline uint64_t RBAR0_64(const DeviceContext &c, uint64_t offset) {
    return access_vram(c, offset, 8, 8) ? *reinterpret_cast<const volatile uint64_t *>(c.bar0 + offset) : UINT64_MAX;
}
inline void WBAR0_32(const DeviceContext &c, uint64_t offset, uint32_t value) {
    if (!access_vram(c, offset, 4)) return;
    *reinterpret_cast<volatile uint32_t *>(c.bar0 + offset) = value;
    storeFence();
}
inline void WBAR0_64(const DeviceContext &c, uint64_t offset, uint64_t value) {
    if (!access_vram(c, offset, 8, 8)) return;
    *reinterpret_cast<volatile uint64_t *>(c.bar0 + offset) = value;
    storeFence();
}
inline bool access_source(const DeviceContext &c, const void *source, uint64_t bytes) {
    if (!access_ready(c, AccessOp::Copy)) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(source);
    if (!source || !bytes || bytes > UINTPTR_MAX - address)
        return access_fail(c, AccessError::Source, AccessOp::Copy, 0, bytes);
    auto aliases = [&](const volatile void *window, uint64_t length) {
        if (!window) return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(window);
        if (length > UINTPTR_MAX - base) return true;
        return address < base + length && base < address + bytes;
    };
    // Ordinary, caller-owned host memory only. Reject aliases of all known
    // MMIO windows; the caller still guarantees the source allocation's lifetime/size.
    if (aliases(c.bar0, c.bar0Size) || aliases(c.rmmio, c.rmmioSize) || aliases(c.bar2, c.bar2Size))
        return access_fail(c, AccessError::Source, AccessOp::Copy, 0, bytes);
    return true;
}
inline void bar0_memcpy_to_vram(const DeviceContext &c, uint64_t offset, const void *source, uint64_t bytes) {
    // Reject odd offsets/lengths before touching memory; never round/truncate.
    if (!access_vram(c, offset, bytes, 4, AccessOp::Copy) || !access_source(c, source, bytes)) return;
    const auto *input = static_cast<const uint8_t *>(source);
    for (uint64_t i = 0; i < bytes; i += 4) {
        uint32_t word;
        memcpy(&word, input + i, 4); // host source may be unaligned
        *reinterpret_cast<volatile uint32_t *>(c.bar0 + offset + i) = word;
    }
    storeFence();
}
inline void bar0_memset_vram(const DeviceContext &c, uint64_t offset, uint32_t pattern, uint64_t bytes) {
    if (!access_vram(c, offset, bytes, 4, AccessOp::Fill)) return;
    for (uint64_t i = 0; i < bytes; i += 4)
        *reinterpret_cast<volatile uint32_t *>(c.bar0 + offset + i) = pattern;
    storeFence();
}
inline void vram_memcpy(const DeviceContext &c, uint64_t offset, const void *source, size_t bytes) {
    // No fallback to MM_INDEX outside the explicitly owned BAR0 window.
    bar0_memcpy_to_vram(c, offset, source, bytes);
}

// Indirect accesses also WRITE index registers. No qualified locking/address
// contract exists for them yet: refuse before even the first index write.
inline uint32_t RVRAM32_via_mm(const DeviceContext &c, uint64_t offset) {
    access_fail(c, AccessError::UnsupportedIndirect, AccessOp::Indirect, offset, 4); return UINT32_MAX;
}
inline void WVRAM32_via_mm(const DeviceContext &c, uint64_t offset, uint32_t) {
    access_fail(c, AccessError::UnsupportedIndirect, AccessOp::Indirect, offset, 4);
}
inline uint32_t SMN_RREG32(const DeviceContext &c, uint32_t reg) { return RVRAM32_via_mm(c, uint64_t{reg} * 4); }
inline void SMN_WREG32(const DeviceContext &c, uint32_t reg, uint32_t value) { WVRAM32_via_mm(c, uint64_t{reg} * 4, value); }
inline uint32_t PCIE_PORT_RREG32(const DeviceContext &c, uint32_t reg) { return SMN_RREG32(c, reg); }
inline void PCIE_PORT_WREG32(const DeviceContext &c, uint32_t reg, uint32_t value) { SMN_WREG32(c, reg, value); }

inline bool hdp_flush_ready(const DeviceContext &c) {
    if (!access_ready(c, AccessOp::Flush)) return false;
    if (!c.hdpFlushQualified)
        return access_fail(c, AccessError::UnqualifiedFlush, AccessOp::Flush);
    return register_span(c, c.hdpFlushDword) && register_span(c, c.hdpReadbackDword);
}
inline void amdgpu_hdp_flush(const DeviceContext &c) {
    // Validate the entire compound operation BEFORE its first write.
    if (!hdp_flush_ready(c)) return;
    storeFence();
    WREG32(c, c.hdpFlushDword, 0);
    (void)RREG32(c, c.hdpReadbackDword);
}
inline void WDOORBELL32(const DeviceContext &c, uint64_t offset, uint32_t value) {
    if (!access_window(c, c.bar2, c.bar2Size, offset, 4, 4, AccessOp::Doorbell)) return;
    *reinterpret_cast<volatile uint32_t *>(c.bar2 + offset) = value; storeFence();
}
inline void WDOORBELL64(const DeviceContext &c, uint64_t offset, uint64_t value) {
    if (!access_window(c, c.bar2, c.bar2Size, offset, 8, 8, AccessOp::Doorbell)) return;
    *reinterpret_cast<volatile uint64_t *>(c.bar2 + offset) = value; storeFence();
}
inline uint32_t RDOORBELL32(const DeviceContext &c, uint64_t offset) {
    return access_window(c, c.bar2, c.bar2Size, offset, 4, 4, AccessOp::Doorbell)
        ? *reinterpret_cast<const volatile uint32_t *>(c.bar2 + offset) : UINT32_MAX;
}

inline kern_return_t poll_checked(const DeviceContext &c, uint32_t reg, uint32_t mask,
                                  uint32_t expected, uint64_t timeoutUs, uint32_t *out, bool psp) {
    if (out) *out = 0;
    if (!access_ready(c, AccessOp::Poll)) return access_status(c);
    constexpr uint64_t kMaxPollUs = 20 * 1000000;
    if (!mask || (expected & ~mask) || timeoutUs > kMaxPollUs) {
        access_fail(c, AccessError::PollArgument, AccessOp::Poll, reg, timeoutUs);
        return kIOReturnBadArgument;
    }
    uint64_t remaining = timeoutUs;
    for (;;) {
        const uint32_t value = RREG32(c, reg);
        // An error sentinel must NEVER match the requested response.
        if (access_status(c) != kIOReturnSuccess) return access_status(c);
        if (out) *out = value;
        if (psp && (value & 0x80000000u) && (value & 0xffffu)) return kIOReturnIOError;
        if ((value & mask) == expected) return kIOReturnSuccess;
        if (!remaining) {
            access_fail(c, AccessError::Timeout, AccessOp::Poll, reg, timeoutUs);
            return kIOReturnTimeout;
        }
        IOSleep(1);
        remaining = remaining > 1000 ? remaining - 1000 : 0;
    }
}
inline kern_return_t poll_psp_response(const DeviceContext &c, uint32_t reg, uint32_t mask,
                                     uint32_t expected, uint64_t timeoutUs, uint32_t *out) {
    return poll_checked(c, reg, mask, expected, timeoutUs, out, true);
}
inline bool poll_reg(const DeviceContext &c, uint32_t reg, uint32_t mask, uint32_t expected,
                     uint64_t timeoutUs, uint32_t *out = nullptr) {
    return poll_checked(c, reg, mask, expected, timeoutUs, out, false) == kIOReturnSuccess;
}
} // namespace amdgpu
