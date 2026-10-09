// SPDX-License-Identifier: MIT
#include "MappedAccess.hpp"

namespace n48native {
namespace {
bool overlap(const volatile void *a, uint64_t aBytes, const volatile void *b, uint64_t bBytes) {
    if (!a || !b) return false;
    // Both complete windows were checked for pointer overflow first.
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x < y + bBytes && y < x + aBytes;
}
}
BindResult bind_access(amdgpu::DeviceContext &c, const Claims &claims, const MappedWindows &w) {
    Plan plan{};
    if (evaluate(claims, plan) != Decision::ClaimsConsistent) return BindResult::Preconditions;
    if (c.accessEnabled || c.accessFault.error != amdgpu::AccessError::None || c.rmmio || c.bar0 || c.bar2)
        return BindResult::ContextUsed;
    // Current upstream code assumes aperture offset zero. Reject instead of aliasing.
    if (claims.aperture.offset != 0) return BindResult::UnsupportedWindowOrigin;
    if (w.vramBytes != claims.aperture.bytes ||
        !amdgpu::window_shape(w.registers, w.registerBytes, 4) || (w.registerBytes & 3) ||
        !amdgpu::window_shape(w.vram, w.vramBytes, 8) || (w.vramBytes & 7) ||
        ((w.doorbells || w.doorbellBytes) &&
         (!amdgpu::window_shape(w.doorbells, w.doorbellBytes, 8) || (w.doorbellBytes & 7))) ||
        overlap(w.registers, w.registerBytes, w.vram, w.vramBytes) ||
        overlap(w.registers, w.registerBytes, w.doorbells, w.doorbellBytes) ||
        overlap(w.vram, w.vramBytes, w.doorbells, w.doorbellBytes)) return BindResult::InvalidWindows;
    if (!w.mcBaseKnown || !w.mcBase || claims.vramBytes > UINT64_MAX - w.mcBase)
        return BindResult::InvalidMcBase;
    c.rmmio = w.registers; c.rmmioSize = w.registerBytes;
    c.bar0 = w.vram; c.bar0Size = w.vramBytes;
    c.bar2 = w.doorbells; c.bar2Size = w.doorbellBytes;
    c.vramSizeBytes = claims.vramBytes; c.vramMcBase = w.mcBase;
    c.vramBase = plan.scratch.offset; c.vramLimit = plan.scratch.offset + plan.scratch.bytes;
    c.accessEnabled = true; // last; no access occurs while validating the binding
    return BindResult::BoundDeclaredWindows;
}
} // namespace n48native
