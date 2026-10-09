// SPDX-License-Identifier: MIT
#pragma once
#include "Preflight.hpp"
#include "AmdGpuAccess.hpp"

namespace n48native {
struct MappedWindows {
    volatile uint32_t *registers;
    uint64_t registerBytes;
    volatile uint8_t *vram;
    uint64_t vramBytes;
    volatile uint8_t *doorbells;
    uint64_t doorbellBytes;
    bool mcBaseKnown;
    uint64_t mcBase;
};
enum class BindResult { BoundDeclaredWindows, Preconditions, ContextUsed, UnsupportedWindowOrigin,
                        InvalidWindows, InvalidMcBase };
// Binds already-owned, live mappings AFTER the pure preconditions. Does not map,
// inspect PCI, establish the truth of claims, qualify coherency, or touch memory.
// Caller must serialize and keep all mappings alive. Never call this with BAR
// physical addresses cast to pointers. The separate IOKit resource controller
// does not call this binder: its hardware authorization/lifecycle is incomplete.
BindResult bind_access(amdgpu::DeviceContext &context, const Claims &claims, const MappedWindows &windows);
} // namespace n48native
