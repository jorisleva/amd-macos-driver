// SPDX-License-Identifier: MIT
// Executes the SAME access helpers and patched PSP TU as the kernel archive.
// All windows are ordinary process RAM. No PCI/device mapping, no actual GPU.
#include "MappedAccess.hpp"
#include "amd/amdgpu_psp.h"
#undef vsnprintf // logger injection must not rewrite the test's libc call
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace amdgpu;
using namespace n48native;
static unsigned checks = 0, failed = 0, sleeps = 0;
static void (*sleepHook)() = nullptr;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
void IOLog(const char *, ...) {} // no test relies on a diagnostic as proof of success
int native_test_vsnprintf(char *p, size_t n, const char *fmt, va_list args) { return std::vsnprintf(p, n, fmt, args); }
void IOSleep(unsigned ms) { CHECK(ms > 0); ++sleeps; if (sleepHook) sleepHook(); }
constexpr uint64_t MiB = 1024 * 1024;
struct Fixture {
    std::vector<uint32_t> regs = std::vector<uint32_t>(8192);
    std::vector<uint64_t> ram = std::vector<uint64_t>(32 * MiB / 8);
    uint64_t doorbells[16]{};
    Claims claims{};
    MappedWindows windows{};
    DeviceContext dev{};
    PSPContext psp{};
    uint8_t *vram() { return reinterpret_cast<uint8_t *>(ram.data()); }
    void fresh() {
        // A fresh synthetic session, never a reset/recovery API on the real card.
        dev = {}; psp = {}; claims = {};
        std::fill(regs.begin(), regs.end(), 0);
        std::fill(ram.begin(), ram.end(), 0);
        std::memset(doorbells, 0, sizeof(doorbells));
        sleeps = 0; sleepHook = nullptr;
        claims.argumentPresent = true; claims.argumentValue = 1;
        for (unsigned i = 0; i < 6; ++i) claims.identity[i] = kIdentity[i];
        claims.consoleKnown = true; claims.console = {0, MiB};
        claims.vramSizeKnown = true; claims.vramBytes = 128 * MiB;
        claims.apertureVramOffsetKnown = true; claims.aperture = {0, 32 * MiB};
        claims.scratch = {MiB, 24 * MiB}; claims.requiredBytes = 24 * MiB;
        claims.exclusiveOwnership = true; claims.dma = Dma::QualifiedMapping;
        claims.recovery = Recovery::RestorationTested; claims.teardownQualified = true;
        windows = {regs.data(), regs.size() * 4, vram(), ram.size() * 8,
                   reinterpret_cast<uint8_t *>(doorbells), sizeof(doorbells), true, 0x8000000000ULL};
    }
    void bind() {
        CHECK(bind_access(dev, claims, windows) == BindResult::BoundDeclaredWindows);
        dev.ip.set(IPBlock::MP0, 0x100);
        dev.ip.set(IPBlock::MMHUB, 0x1000);
        regs[0x1000 + MMHUBRegs::MMMC_VM_FB_LOCATION_BASE] = 0x8000;
    }
    void coherent() { // simulated register choice; not qualified on the real card
        dev.hdpFlushQualified = true; dev.hdpFlushDword = 0x10; dev.hdpReadbackDword = 0x11;
    }
    void init() { bind(); coherent(); CHECK(psp_init(dev, psp) == kIOReturnSuccess); }
    void noWrites() {
        CHECK(std::all_of(ram.begin(), ram.end(), [](uint64_t x) { return x == 0; }));
        CHECK(std::all_of(std::begin(doorbells), std::end(doorbells), [](uint64_t x) { return x == 0; }));
    }
};
static Fixture *active;

int main() {
    Fixture f;
    f.fresh();
    WREG32(f.dev, 0, 1);
    CHECK(f.dev.accessFault.error == AccessError::Disabled);
    CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::ContextUsed);
    CHECK(f.regs[0] == 0); f.noWrites();
    // Missing evidence cannot enable even otherwise valid mappings.
    for (unsigned which = 0; which < 6; ++which) {
        f.fresh();
        if (which == 0) f.claims.consoleKnown = false;
        if (which == 1) f.claims.dma = Dma::AssumedCpuPhysical;
        if (which == 2) f.claims.recovery = Recovery::ReferenceBootOnly;
        if (which == 3) f.claims.teardownQualified = false;
        if (which == 4) f.claims.identity[1] = 0x7551;
        if (which == 5) f.claims.argumentValue = 0;
        CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::Preconditions);
        CHECK(!f.dev.accessEnabled && !f.dev.bar0 && !f.dev.rmmio); f.noWrites();
    }
    for (unsigned which = 0; which < 8; ++which) {
        f.fresh();
        if (which == 0) f.windows.registers = nullptr;
        if (which == 1) --f.windows.vramBytes;
        if (which == 2) f.windows.vram = f.vram() + 1;
        if (which == 3) f.windows.registers = reinterpret_cast<uint32_t *>(UINTPTR_MAX - 3);
        if (which == 4) f.windows.doorbellBytes = 0;
        if (which == 5) f.windows.doorbells = f.vram();
        if (which == 6) f.windows.registers = reinterpret_cast<uint32_t *>(f.vram());
        if (which == 7) f.windows.registerBytes = 0;
        CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::InvalidWindows);
        CHECK(!f.dev.accessEnabled && !f.dev.bar0); f.noWrites();
    }
    f.fresh(); f.windows.mcBaseKnown = false;
    CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::InvalidMcBase);
    f.windows.mcBaseKnown = true; f.windows.mcBase = UINT64_MAX;
    CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::InvalidMcBase);
    f.fresh(); f.claims.aperture.offset = 4096;
    CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::UnsupportedWindowOrigin);
    f.fresh(); f.bind();
    CHECK(bind_access(f.dev, f.claims, f.windows) == BindResult::ContextUsed);
    WREG32(f.dev, 8191, 0x12345678); CHECK(RREG32(f.dev, 8191) == 0x12345678);
    WBAR0_32(f.dev, MiB, 0x12345678); CHECK(RBAR2_32(f.dev, MiB) == 0x12345678);
    WBAR0_64(f.dev, f.dev.vramLimit - 8, 0x8765432101234567ULL);
    CHECK(RBAR0_64(f.dev, f.dev.vramLimit - 8) == 0x8765432101234567ULL);
    WDOORBELL64(f.dev, sizeof(f.doorbells) - 8, 0x1234567887654321ULL);
    CHECK(RDOORBELL32(f.dev, sizeof(f.doorbells) - 8) == 0x87654321);
    CHECK(f.dev.vramMC(MiB) == f.windows.mcBase + MiB);
    // Boundary and overflow failures have no partial write and stop later I/O.
    for (uint64_t off : {uint64_t{0}, MiB - 4, MiB + 1, uint64_t{25 * MiB},
                         uint64_t{UINT64_MAX}, uint64_t{UINT64_MAX - 3}, uint64_t{1ULL << 32}}) {
        f.fresh(); f.bind(); WBAR0_32(f.dev, off, 0xbeef);
        CHECK(access_status(f.dev) != kIOReturnSuccess);
        const AccessFault first = f.dev.accessFault;
        WREG32(f.dev, 0, 1); WDOORBELL32(f.dev, 0, 1); WBAR0_32(f.dev, MiB, 1);
        CHECK(f.dev.accessFault.error == first.error && f.dev.accessFault.offset == first.offset);
        CHECK(f.regs[0] == 0); f.noWrites();
    }
    f.fresh(); f.bind(); WREG32(f.dev, UINT32_MAX, 1); CHECK(f.regs[0] == 0);
    CHECK(access_status(f.dev) == kIOReturnIOError);
    f.fresh(); f.bind(); WDOORBELL64(f.dev, UINT64_MAX - 7, 1); f.noWrites();
    f.fresh(); f.bind(); WDOORBELL32(f.dev, 1, 1); f.noWrites();
    f.fresh(); f.bind();
    CHECK(SOC15_REG_OFFSET_BIDX(f.dev, static_cast<IPBlock>(255), 0, 1) == UINT32_MAX);
    CHECK(f.dev.accessFault.error == AccessError::IpBase);
    f.fresh(); f.bind(); f.dev.ip.set(IPBlock::MP0, UINT32_MAX - 2);
    CHECK(SOC15_REG_OFFSET(f.dev, IPBlock::MP0, 4) == UINT32_MAX);
    f.fresh(); f.bind(); CHECK(SOC15_REG_OFFSET_BIDX(f.dev, IPBlock::MP0, -1, 0) == UINT32_MAX);
    f.fresh(); f.bind(); f.dev.vramMcBase = UINT64_MAX - 3;
    CHECK(f.dev.vramMC(MiB) == UINT64_MAX); CHECK(f.dev.accessFault.error == AccessError::McAddress);

    uint8_t payload[64]; for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i + 1);
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, payload + 1, 60);
    CHECK(std::memcmp(f.vram() + MiB, payload + 1, 60) == 0);
    CHECK(f.vram()[MiB - 1] == 0 && f.vram()[MiB + 60] == 0);
    bar0_memset_vram(f.dev, MiB, 0x44332211, 64);
    CHECK(RBAR0_32(f.dev, MiB + 60) == 0x44332211 && f.vram()[MiB + 64] == 0);
    for (uint64_t bytes : {uint64_t{0}, uint64_t{1}, uint64_t{2}, uint64_t{3}, uint64_t{5},
                           uint64_t{UINT64_MAX}, uint64_t{UINT64_MAX - 3}}) {
        f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, f.dev.vramLimit - 4, payload, bytes);
        CHECK(access_status(f.dev) != kIOReturnSuccess); f.noWrites();
        f.fresh(); f.bind(); bar0_memset_vram(f.dev, f.dev.vramLimit - 4, 123, bytes);
        CHECK(access_status(f.dev) != kIOReturnSuccess); f.noWrites();
    }
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, nullptr, 4); f.noWrites();
    CHECK(f.dev.accessFault.error == AccessError::Source);
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, f.vram() + MiB + 4, 4); f.noWrites();
    CHECK(f.dev.accessFault.error == AccessError::Source);
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, f.regs.data(), 4); f.noWrites();
    CHECK(f.dev.accessFault.error == AccessError::Source);
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, f.doorbells, 4); f.noWrites();
    CHECK(f.dev.accessFault.error == AccessError::Source);
    f.fresh(); f.bind(); bar0_memcpy_to_vram(f.dev, MiB, reinterpret_cast<void *>(UINTPTR_MAX - 1), 4);
    CHECK(f.dev.accessFault.error == AccessError::Source); f.noWrites();
    f.fresh(); f.bind(); vram_memcpy(f.dev, 64 * MiB, payload, 4);
    CHECK(f.regs[0] == 0 && f.regs[6] == 0); f.noWrites();
    for (unsigned which = 0; which < 6; ++which) {
        f.fresh(); f.bind();
        if (which == 0) (void)RVRAM32_via_mm(f.dev, UINT64_MAX);
        if (which == 1) WVRAM32_via_mm(f.dev, MiB, 1);
        if (which == 2) (void)SMN_RREG32(f.dev, UINT32_MAX);
        if (which == 3) SMN_WREG32(f.dev, 1, 2);
        if (which == 4) (void)PCIE_PORT_RREG32(f.dev, 1);
        if (which == 5) PCIE_PORT_WREG32(f.dev, 1, 2);
        CHECK(access_status(f.dev) == kIOReturnUnsupported);
        CHECK(f.regs[0] == 0 && f.regs[6] == 0 && f.regs[14] == 0); f.noWrites();
    }
    f.fresh(); f.bind(); f.regs[16] = 99; amdgpu_hdp_flush(f.dev);
    CHECK(access_status(f.dev) == kIOReturnUnsupported && f.regs[16] == 99);
    f.fresh(); f.bind(); f.coherent(); f.dev.hdpReadbackDword = UINT32_MAX; f.regs[16] = 99;
    amdgpu_hdp_flush(f.dev); CHECK(f.regs[16] == 99); // no first write if readback invalid
    uint32_t value = 123;
    f.fresh(); f.bind();
    CHECK(!poll_reg(f.dev, UINT32_MAX, UINT32_MAX, UINT32_MAX, 1000, &value));
    CHECK(value == 0 && sleeps == 0); // error sentinel was not an acknowledgement
    f.fresh(); f.bind();
    CHECK(poll_psp_response(f.dev, UINT32_MAX, 0x80000000, 0x80000000, 1000, &value) == kIOReturnIOError);
    CHECK(value == 0 && sleeps == 0);
    f.fresh(); f.bind(); CHECK(!poll_reg(f.dev, 1, 1, 1, UINT64_MAX)); CHECK(sleeps == 0);
    f.fresh(); f.bind(); CHECK(!poll_reg(f.dev, 1, 1, 1, 1000)); CHECK(sleeps == 1);
    CHECK(f.dev.accessFault.error == AccessError::Timeout);
    f.fresh(); f.bind(); f.regs[1] = 0x80000001;
    CHECK(poll_psp_response(f.dev, 1, 0x80000000, 0x80000000, 0, &value) == kIOReturnIOError);

    // Real PSP implementation: layout refusal before publication; strict live MC check.
    f.fresh(); CHECK(psp_init(f.dev, f.psp) == kIOReturnNotReady); CHECK(f.psp.fwPriSize == 0);
    f.fresh(); f.bind(); f.dev.vramLimit = 0;
    CHECK(psp_init(f.dev, f.psp) != kIOReturnSuccess); CHECK(f.psp.fwPriSize == 0);
    f.fresh(); f.bind(); f.dev.vramBase += 4096;
    CHECK(psp_init(f.dev, f.psp) == kIOReturnBadArgument); CHECK(f.psp.fwPriSize == 0);
    f.fresh(); f.bind(); f.regs[0x1554] = 0x8001;
    CHECK(psp_init(f.dev, f.psp) != kIOReturnSuccess); CHECK(f.psp.fwPriSize == 0); f.noWrites();
    f.fresh(); f.init(); CHECK(psp_init(f.dev, f.psp) == kIOReturnBusy);
    uint64_t bus = 123;
    CHECK(psp_fw_buf_stage(f.dev, f.psp, payload, 64, &bus) == kIOReturnSuccess);
    CHECK(bus == f.dev.vramMcBase + f.psp.fwBufVRAMOffset);
    CHECK(std::memcmp(f.vram() + f.psp.fwBufVRAMOffset, payload, 64) == 0);
    CHECK(f.psp.fwBufBumpOffset == 4096 && f.vram()[f.psp.fwBufVRAMOffset + 64] == 0);
    CHECK(psp_fw_buf_stage(f.dev, f.psp, payload, 4, &bus) == kIOReturnSuccess);
    CHECK(bus == f.psp.fwBufBaseMC + 4096 && f.psp.fwBufBumpOffset == 8192);
    for (unsigned which = 0; which < 7; ++which) {
        f.fresh(); f.init(); bus = 123;
        if (which == 0) f.dev.hdpFlushQualified = false;
        if (which == 1) f.psp.fwBufBumpOffset = UINT64_MAX;
        if (which == 2) f.psp.fwBufBumpOffset = f.psp.fwBufSize;
        if (which == 3) f.psp.fwBufVRAMOffset = UINT64_MAX - 3;
        if (which == 4) ++f.psp.fwBufBaseMC;
        const auto oldBump = f.psp.fwBufBumpOffset;
        const uint32_t size = which == 5 ? UINT32_MAX - 3 : (which == 6 ? 3 : 64);
        CHECK(psp_fw_buf_stage(f.dev, f.psp, payload, size, &bus) != kIOReturnSuccess);
        CHECK(bus == 0 && f.psp.fwBufBumpOffset == oldBump); f.noWrites();
    }
    // Real ring submission code, with an explicitly simulated response in RAM.
    uint32_t command[kPSPGfxCmdRespSize / 4]{};
    active = &f;
    for (unsigned which = 0; which < 7; ++which) {
        f.fresh(); f.init(); f.psp.ringCreated = true;
        if (which == 0) f.psp.ringSize = 0;
        if (which == 1) f.psp.ringSize = (1ULL << 32) + kPSPKMRingSize;
        if (which == 2) f.psp.fenceCounter = UINT32_MAX;
        if (which == 3) f.regs[0x100 + MP0Regs::C2PMSG_67] = 1;
        if (which == 4) f.regs[0x100 + MP0Regs::C2PMSG_67] = 1024;
        if (which == 5) f.psp.cmdVRAMOffset = f.psp.ringVRAMOffset;
        if (which == 6) f.dev.hdpFlushQualified = false;
        value = 123;
        CHECK(psp_ring_cmd_submit(f.dev, f.psp, command, sizeof(command), &value) != kIOReturnSuccess);
        CHECK(value == UINT32_MAX && sleeps == 0); f.noWrites();
    }
    f.fresh(); f.init(); f.psp.ringCreated = true;
    sleepHook = [] { *reinterpret_cast<uint32_t *>(active->vram() + active->psp.fenceVRAMOffset) = active->psp.fenceCounter; };
    CHECK(psp_ring_cmd_submit(f.dev, f.psp, command, sizeof(command), &value) == kIOReturnSuccess);
    CHECK(value == 0 && f.psp.fenceCounter == 1 && sleeps == 1);
    CHECK(f.regs[0x100 + MP0Regs::C2PMSG_67] == sizeof(PSPGfxRBFrame) / 4);
    PSPGfxRBFrame frame{};
    std::memcpy(&frame, f.vram() + f.psp.ringVRAMOffset, sizeof(frame));
    CHECK(frame.fence_value == 1 && frame.cmd_buf_size == 0);
    CHECK(((uint64_t{frame.cmd_buf_addr_hi} << 32) | frame.cmd_buf_addr_lo) == f.psp.cmdBusAddr);
    CHECK(((uint64_t{frame.fence_addr_hi} << 32) | frame.fence_addr_lo) == f.psp.fenceBusAddr);
    // A device error is propagated, not represented as a completed success.
    f.fresh(); f.init(); f.psp.ringCreated = true;
    sleepHook = [] {
        *reinterpret_cast<uint32_t *>(active->vram() + active->psp.fenceVRAMOffset) = active->psp.fenceCounter;
        *reinterpret_cast<uint32_t *>(active->vram() + active->psp.cmdVRAMOffset + kPSPGfxRespStatusOffset) = 9;
    };
    CHECK(psp_ring_cmd_submit(f.dev, f.psp, command, sizeof(command), &value) == kIOReturnError);
    CHECK(value == 9 && sleeps == 1);
    // Disappearing mapping after publication: even UINT32_MAX must not become a fence ACK.
    f.fresh(); f.init(); f.psp.ringCreated = true; f.psp.fenceCounter = UINT32_MAX - 1;
    sleepHook = [] { active->dev.bar0 = nullptr; };
    CHECK(psp_ring_cmd_submit(f.dev, f.psp, command, sizeof(command), &value) == kIOReturnIOError);
    CHECK(value == UINT32_MAX && f.dev.accessFault.error == AccessError::NullMapping && sleeps == 1);
    const uint32_t published = f.regs[0x100 + MP0Regs::C2PMSG_67];
    WREG32(f.dev, 0x100 + MP0Regs::C2PMSG_67, 123);
    CHECK(f.regs[0x100 + MP0Regs::C2PMSG_67] == published);
    // Known MMIO aliases cannot be read as a host command, even for logging.
    f.fresh(); f.init(); f.psp.ringCreated = true;
    CHECK(psp_ring_cmd_submit(f.dev, f.psp, f.regs.data(), sizeof(command), &value) == kIOReturnIOError);
    CHECK(f.dev.accessFault.error == AccessError::Source && sleeps == 0); f.noWrites();
    std::printf("native_access_test: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
