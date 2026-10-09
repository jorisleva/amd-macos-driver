// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace n48native {
// A pure planning contract, NOT hardware authorization. The declared-window
// binder consumes it; the IOKit resource controller does NOT establish all these
// claims on Tahoe and keeps its private hardware-mapping context disabled.
struct Range { uint64_t offset; uint64_t bytes; }; // VRAM-relative, never PCI addresses
constexpr uint32_t kIdentity[6] = {0x1002, 0x7550, 0x1849, 0x5417, 0xc0, 0x030000};
enum class Dma { Unknown, AssumedCpuPhysical, QualifiedMapping };
enum class Recovery { Unknown, ReferenceBootOnly, RestorationTested };
struct Claims {
    bool argumentPresent;
    uint32_t argumentValue;
    uint32_t identity[6];
    bool consoleKnown;
    Range console;
    bool vramSizeKnown;
    uint64_t vramBytes;
    bool apertureVramOffsetKnown;
    Range aperture;
    Range scratch; // explicitly requested; never a fallback at 64 MiB
    uint64_t requiredBytes; // complete reservation required by the future caller
    bool exclusiveOwnership;
    Dma dma;
    Recovery recovery;
    bool teardownQualified;
};
enum class Decision {
    Disabled, WrongCard, UnknownConsole, InvalidGeometry, InvalidScratch,
    ConsoleOverlap, OwnershipUnproven, DmaUnproven, RecoveryUnproven,
    TeardownUnproven, ClaimsConsistent
};
struct Plan { Range scratch; };
Decision evaluate(const Claims &claims, Plan &output);

// Resource ownership model for a future controller. Mark PotentiallyGpuOwned
// BEFORE its first hardware-visible operation. Quarantine is terminal: do not
// recycle pages or attempt hot unload/reload after an unquiesced failure.
enum class State { Empty, Prepared, PotentiallyGpuOwned, Quiesced, Quarantined, Released,
                   FailedBeforePublication };
enum class Event { PreparedResources, HardwareMayReference, QuiesceConfirmed, Failure, Release };
bool transition(State &state, Event event);
} // namespace n48native
