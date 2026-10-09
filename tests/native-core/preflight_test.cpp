// SPDX-License-Identifier: MIT
#include "Preflight.hpp"
#include "NativeLog.hpp"
#include <cstdarg>
#include <cstdio>
#include <string>

using namespace n48native;
static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
static std::string logged;
static bool formattingFailure = false;
void IOLog(const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    logged += text;
}
int native_test_vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    return formattingFailure ? -1 : std::vsnprintf(buffer, size, format, args);
}

static constexpr uint64_t MiB = 1024 * 1024;
static Claims synthetic() {
    // These are SYNTHETIC positive claims, never measurements from this PC.
    Claims c{};
    c.argumentPresent = true; c.argumentValue = 1;
    for (unsigned i = 0; i < 6; ++i) c.identity[i] = kIdentity[i];
    c.consoleKnown = true; c.console = {0, 8 * MiB};
    c.vramSizeKnown = true; c.vramBytes = 16 * 1024 * MiB;
    c.apertureVramOffsetKnown = true; c.aperture = {0, 256 * MiB};
    c.scratch = {64 * MiB, 32 * MiB}; c.requiredBytes = 32 * MiB;
    c.exclusiveOwnership = true; c.dma = Dma::QualifiedMapping;
    c.recovery = Recovery::RestorationTested; c.teardownQualified = true;
    return c;
}
static void reject(const Claims &c, Decision expected) {
    Plan p{{123, 456}};
    CHECK(evaluate(c, p) == expected);
    CHECK(p.scratch.offset == 0);
    CHECK(p.scratch.bytes == 0);
}
static void accept(const Claims &c) {
    Plan p{};
    CHECK(evaluate(c, p) == Decision::ClaimsConsistent);
    CHECK(p.scratch.offset == c.scratch.offset);
    CHECK(p.scratch.bytes == c.scratch.bytes);
}

int main() {
    Claims c{};
    reject(c, Decision::Disabled);
    accept(synthetic());
    c = synthetic(); c.argumentPresent = false; reject(c, Decision::Disabled);
    for (uint32_t value : {0u, 2u, 3u, 0x100u, 0x80000000u, UINT32_MAX}) {
        c = synthetic(); c.argumentValue = value; reject(c, Decision::Disabled);
    }
    for (unsigned field = 0; field < 6; ++field) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            c = synthetic(); c.identity[field] ^= uint32_t{1} << bit;
            reject(c, Decision::WrongCard);
        }
    }
    c = synthetic(); c.identity[1] = 0x7551; reject(c, Decision::WrongCard);
    c = synthetic(); c.consoleKnown = false; reject(c, Decision::UnknownConsole);
    // Return-to-reference and registry metadata alone do not prove readiness.
    c = {}; c.argumentPresent = true; c.argumentValue = 1;
    for (unsigned i = 0; i < 6; ++i) c.identity[i] = kIdentity[i];
    c.aperture.bytes = 256 * MiB; c.recovery = Recovery::ReferenceBootOnly;
    reject(c, Decision::UnknownConsole);
    c = synthetic(); c.vramSizeKnown = false; reject(c, Decision::InvalidGeometry);
    c = synthetic(); c.apertureVramOffsetKnown = false; reject(c, Decision::InvalidGeometry);
    c = synthetic(); c.vramBytes = 0; reject(c, Decision::InvalidGeometry);
    c = synthetic(); c.vramBytes = 256 * MiB - 1; reject(c, Decision::InvalidGeometry);
    for (Range r : {Range{0, 0}, Range{UINT64_MAX, 1}, Range{UINT64_MAX - 7, 8},
                    Range{16 * 1024 * MiB, 4096}}) {
        c = synthetic(); c.console = r; reject(c, Decision::InvalidGeometry);
        c = synthetic(); c.aperture = r; reject(c, Decision::InvalidGeometry);
    }
    c = synthetic(); c.requiredBytes = 0; reject(c, Decision::InvalidScratch);
    c = synthetic(); ++c.requiredBytes; reject(c, Decision::InvalidScratch);
    for (Range r : {Range{64 * MiB, 0}, Range{64 * MiB + 1, 32 * MiB},
                    Range{64 * MiB, 32 * MiB - 1}, Range{UINT64_MAX - 4095, 32 * MiB},
                    Range{224 * MiB + 4096, 32 * MiB}, Range{256 * MiB, 32 * MiB}}) {
        c = synthetic(); c.scratch = r; reject(c, Decision::InvalidScratch);
    }
    // Half-open range edges: adjacency allowed, one-page overlap forbidden.
    for (uint64_t start = 0; start <= 256 * MiB; start += MiB) {
        c = synthetic(); c.console = {64 * MiB, 8 * MiB}; c.scratch = {start, 32 * MiB};
        if (start > 224 * MiB) reject(c, Decision::InvalidScratch);
        else if (start < 72 * MiB && start + 32 * MiB > 64 * MiB) reject(c, Decision::ConsoleOverlap);
        else accept(c);
    }
    c = synthetic(); c.scratch.offset = c.console.bytes; accept(c);
    c = synthetic(); c.scratch.offset = c.console.bytes - 4096; reject(c, Decision::ConsoleOverlap);
    // Offset and length remain 64-bit; BAR PCI addresses must not substitute for them.
    c = synthetic(); c.aperture = {8 * 1024 * MiB, 256 * MiB};
    c.scratch.offset = c.aperture.offset + 64 * MiB; accept(c);
    c.scratch.offset = 64 * MiB; reject(c, Decision::InvalidScratch);
    c = synthetic(); c.exclusiveOwnership = false; reject(c, Decision::OwnershipUnproven);
    for (Dma dma : {Dma::Unknown, Dma::AssumedCpuPhysical, static_cast<Dma>(99)}) {
        c = synthetic(); c.dma = dma; reject(c, Decision::DmaUnproven);
    }
    for (Recovery recovery : {Recovery::Unknown, Recovery::ReferenceBootOnly, static_cast<Recovery>(99)}) {
        c = synthetic(); c.recovery = recovery; reject(c, Decision::RecoveryUnproven);
    }
    c = synthetic(); c.teardownQualified = false; reject(c, Decision::TeardownUnproven);

    // Complete ownership transition matrix: -1 means refusal without mutation.
    const int expected[7][5] = {
        {1, -1, -1, -1, -1}, {-1, 2, -1, 6, 5}, {-1, -1, 3, 4, -1},
        {-1, -1, -1, 4, 5}, {-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1},
        {-1, -1, -1, -1, 5}
    };
    for (int s = 0; s < 7; ++s) {
        for (int e = 0; e < 5; ++e) {
            State state = static_cast<State>(s);
            CHECK(transition(state, static_cast<Event>(e)) == (expected[s][e] >= 0));
            CHECK(static_cast<int>(state) == (expected[s][e] < 0 ? s : expected[s][e]));
        }
        State state = static_cast<State>(s);
        CHECK(!transition(state, static_cast<Event>(99)));
        CHECK(static_cast<int>(state) == s);
    }
    State unknown = static_cast<State>(99);
    CHECK(!transition(unknown, Event::Release));
    CHECK(static_cast<int>(unknown) == 99);
    State before = State::Prepared;
    CHECK(transition(before, Event::Failure));
    CHECK(before == State::FailedBeforePublication);
    CHECK(!transition(before, Event::HardwareMayReference));
    CHECK(transition(before, Event::Release));
    CHECK(!transition(before, Event::Release));
    State state = State::Empty;
    CHECK(transition(state, Event::PreparedResources));
    CHECK(transition(state, Event::HardwareMayReference));
    CHECK(!transition(state, Event::Release));
    CHECK(transition(state, Event::Failure));
    CHECK(state == State::Quarantined);
    CHECK(!transition(state, Event::QuiesceConfirmed));
    CHECK(!transition(state, Event::Release));

    amdgpu::n48_logf("native %u %s\n", 7u, "ok");
    CHECK(logged == "native 7 ok\n");
    logged.clear(); amdgpu::n48_logf("%s", "%n%p%s");
    CHECK(logged == "%n%p%s");
    logged.clear(); amdgpu::n48_logf("%s", std::string(511, 'a').c_str());
    CHECK(logged == std::string(511, 'a'));
    logged.clear(); amdgpu::n48_logf("%s", std::string(512, 'b').c_str());
    CHECK(logged == std::string(511, 'b') + " [truncated]\n");
    logged.clear(); formattingFailure = true; amdgpu::n48_logf("failure");
    CHECK(logged == "Navi48FirmwareCore: log formatting failed\n");
    std::printf("native_core_test: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
