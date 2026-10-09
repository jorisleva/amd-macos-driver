// SPDX-License-Identifier: MIT
#include "Preflight.hpp"

namespace n48native {
namespace {
bool valid(Range r) { return r.bytes != 0 && r.offset <= UINT64_MAX - r.bytes; }
bool contains(Range outer, Range inner) {
    return valid(outer) && valid(inner) && inner.offset >= outer.offset &&
        inner.offset - outer.offset <= outer.bytes &&
        inner.bytes <= outer.bytes - (inner.offset - outer.offset);
}
bool overlaps(Range a, Range b) {
    // Only called after validation of both half-open ranges.
    return a.offset < b.offset + b.bytes && b.offset < a.offset + a.bytes;
}
} // namespace

Decision evaluate(const Claims &c, Plan &out) {
    out = {}; // no partial/stale plan on any refusal
    if (!c.argumentPresent || c.argumentValue != 1) return Decision::Disabled;
    for (unsigned i = 0; i < 6; ++i)
        if (c.identity[i] != kIdentity[i]) return Decision::WrongCard;
    if (!c.consoleKnown) return Decision::UnknownConsole;
    const Range vram{0, c.vramBytes};
    if (!c.vramSizeKnown || !c.apertureVramOffsetKnown ||
        !contains(vram, c.console) || !contains(vram, c.aperture))
        return Decision::InvalidGeometry;
    if (!c.requiredBytes || c.scratch.bytes < c.requiredBytes ||
        (c.scratch.offset & 0xfff) || (c.scratch.bytes & 0xfff) ||
        !contains(c.aperture, c.scratch)) return Decision::InvalidScratch;
    if (overlaps(c.console, c.scratch)) return Decision::ConsoleOverlap;
    if (!c.exclusiveOwnership) return Decision::OwnershipUnproven;
    if (c.dma != Dma::QualifiedMapping) return Decision::DmaUnproven;
    if (c.recovery != Recovery::RestorationTested) return Decision::RecoveryUnproven;
    if (!c.teardownQualified) return Decision::TeardownUnproven;
    out.scratch = c.scratch;
    return Decision::ClaimsConsistent;
}

bool transition(State &state, Event event) {
    switch (state) {
    case State::Empty:
        if (event == Event::PreparedResources) { state = State::Prepared; return true; }
        break;
    case State::Prepared:
        if (event == Event::HardwareMayReference) { state = State::PotentiallyGpuOwned; return true; }
        if (event == Event::Release) { state = State::Released; return true; }
        // Failure before publication: release is allowed, but never continue setup.
        if (event == Event::Failure) { state = State::FailedBeforePublication; return true; }
        break;
    case State::PotentiallyGpuOwned:
        if (event == Event::QuiesceConfirmed) { state = State::Quiesced; return true; }
        if (event == Event::Failure) { state = State::Quarantined; return true; }
        break;
    case State::Quiesced:
        if (event == Event::Release) { state = State::Released; return true; }
        // New uncertainty about quiescence invalidates permission to release.
        if (event == Event::Failure) { state = State::Quarantined; return true; }
        break;
    case State::FailedBeforePublication:
        if (event == Event::Release) { state = State::Released; return true; }
        break;
    case State::Quarantined:
    case State::Released:
        break;
    }
    return false;
}
} // namespace n48native
