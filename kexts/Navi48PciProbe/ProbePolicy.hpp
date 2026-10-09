// Registry metadata only. Shared unchanged between the kext and host tests.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace n48pci {
constexpr unsigned kIdentityCount = 6;
constexpr unsigned kMaxResources = 7; // six BAR registers and expansion ROM
constexpr unsigned kRecordBytes = 20;
constexpr unsigned kMaxAddressBytes = kMaxResources * kRecordBytes;
constexpr const char *kIdentityKeys[kIdentityCount] = {
    "vendor-id", "device-id", "subsystem-vendor-id", "subsystem-id", "revision-id", "class-code"
};
constexpr uint32_t kTarget[kIdentityCount] = {0x1002, 0x7550, 0x1849, 0x5417, 0xc0, 0x030000};

struct Resource {
    uint32_t flags; // unmodified PCI/OF phys.hi metadata, NOT a config read
    uint32_t reg;
    uint64_t base;
    uint64_t length;
};
struct Snapshot {
    uint32_t identity[kIdentityCount];
    uint32_t count;
    uint32_t bdf; // bus/device/function bits of phys.hi, no config register bits
    Resource resources[kMaxResources];
};

enum class ParseResult {
    Ok, BadLength, BadFlags, BadRegister, DuplicateRegister, MixedBdf,
    BadRange, OverlappingRanges, MissingRequiredBar, WrongRequiredSpace
};

constexpr bool enabled(bool present, uint32_t value) { return present && value == 1; }
inline uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool word(const void *data, size_t length, uint32_t &value) {
    if (!data || length != 4) return false;
    value = le32(static_cast<const uint8_t *>(data));
    return true;
}
inline bool target(const uint32_t identity[kIdentityCount]) {
    if (!identity) return false;
    for (unsigned i = 0; i < kIdentityCount; ++i)
        if (identity[i] != kTarget[i]) return false;
    return true;
}
constexpr bool memory(uint32_t flags) { return ((flags >> 24) & 3u) >= 2; }
constexpr int slot(uint32_t reg) {
    return reg == 0x30 ? 6 : (reg >= 0x10 && reg <= 0x24 && (reg & 3u) == 0)
        ? int((reg - 0x10) / 4) : -1;
}

// x86_64 IOPCIFamily publishes native little-endian 5-cell records. In the
// observed registry, even >4-GiB addresses use space code 2: do NOT interpret
// that code as proof of the hardware BAR's width or discard phys.mid.
// No BAR sizing writes, no MMIO, no attempt to repair malformed metadata.
// On error, the caller's snapshot is unchanged (including count/resources).
inline ParseResult resources(const void *data, size_t length, Snapshot &out) {
    if (!data || length == 0 || length > kMaxAddressBytes || length % kRecordBytes)
        return ParseResult::BadLength;
    Snapshot candidate = out;
    candidate.count = 0;
    uint32_t seen = 0;
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t offset = 0; offset < length; offset += kRecordBytes) {
        const uint8_t *p = bytes + offset;
        Resource r{};
        r.flags = le32(p);
        const uint32_t space = (r.flags >> 24) & 3u;
        if (space == 0 || (r.flags & 0x1c000000u) != 0)
            return ParseResult::BadFlags;
        r.reg = r.flags & 0xffu;
        const int index = slot(r.reg);
        if (index < 0) return ParseResult::BadRegister;
        const uint32_t bit = 1u << unsigned(index);
        if (seen & bit) return ParseResult::DuplicateRegister;
        seen |= bit;
        const uint32_t bdf = r.flags & 0x00ffff00u;
        if (candidate.count == 0) candidate.bdf = bdf;
        else if (candidate.bdf != bdf) return ParseResult::MixedBdf;
        r.base = (uint64_t(le32(p + 4)) << 32) | le32(p + 8);
        r.length = (uint64_t(le32(p + 12)) << 32) | le32(p + 16);
        if (r.base == 0 || r.length == 0 || r.length > UINT64_MAX - r.base)
            return ParseResult::BadRange;
        for (unsigned i = 0; i < candidate.count; ++i) {
            const Resource &other = candidate.resources[i];
            if (memory(r.flags) == memory(other.flags) &&
                r.base < other.base + other.length && other.base < r.base + r.length)
                return ParseResult::OverlappingRanges;
        }
        if ((r.reg == 0x10 || r.reg == 0x18 || r.reg == 0x24 || r.reg == 0x30) && !memory(r.flags))
            return ParseResult::WrongRequiredSpace;
        candidate.resources[candidate.count++] = r;
    }
    constexpr uint32_t required = (1u << 0) | (1u << 2) | (1u << 5);
    if ((seen & required) != required) return ParseResult::MissingRequiredBar;
    out = candidate;
    return ParseResult::Ok;
}
} // namespace n48pci
