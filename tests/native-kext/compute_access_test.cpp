// SPDX-License-Identifier: MIT
// Bounded access on RAM ONLY. No PSP emulation, GPU init, or shader result.
#include "ComputeAccess.hpp"
#include <cstdio>
#include <vector>
using namespace n48compute;
static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
void IOLog(const char *, ...) {}
void IOSleep(unsigned) {}
static bool alive(void *p) { return *static_cast<bool *>(p); }
struct Fixture {
    bool live{true};
    std::vector<uint32_t> registers = std::vector<uint32_t>(0x80000 / 4);
    uint64_t vram[128]{}, doorbell[512]{};
    DeviceContext d{};
    Fixture() {
        d.trialEnabled = true; d.lease = &live; d.leaseAlive = alive;
        d.rmmio = registers.data(); d.rmmioSize = registers.size() * 4;
        d.bar0 = reinterpret_cast<uint8_t *>(vram); d.bar0Size = sizeof(vram);
        d.bar2 = reinterpret_cast<uint8_t *>(doorbell); d.bar2Size = sizeof(doorbell);
        d.vramSizeBytes = 16 * 1024 * 1024; d.vramMcBase = 0x8000000000ULL;
        d.vramBase = 128; d.vramLimit = 1024; d.hdpConfigured = true;
        d.indirectLock = IOLockAlloc();
    }
    ~Fixture() { IOLockFree(d.indirectLock); }
};
int main() {
    { Fixture f; WREG32(f.d, 0, 37); CHECK(RREG32(f.d, 0) == 37);
      WBAR0_64(f.d, 128, 0x1234567890abcdef); CHECK(RBAR0_64(f.d, 128) == 0x1234567890abcdef);
      CHECK(f.d.vramMC(128) == f.d.vramMcBase + 128);
      WDOORBELL32(f.d, 4, 19); CHECK(RDOORBELL32(f.d, 4) == 19);
      WDOORBELL64(f.d, 8, 0x876543210); CHECK(f.doorbell[1] == 0x876543210);
      uint32_t source[2] = {1, 2}; vram_memcpy(f.d, 256, source, 8);
      CHECK(RBAR0_32(f.d, 256) == 1 && RBAR0_32(f.d, 260) == 2);
      bar0_memset_vram(f.d, 256, 43, 8); CHECK(RBAR0_32(f.d, 260) == 43);
      CHECK(access_status(f.d) == kIOReturnSuccess);
    }
    for (uint64_t offset : {uint64_t{0}, uint64_t{124}, uint64_t{1024}, UINT64_MAX}) {
        Fixture f; WBAR0_32(f.d, offset, 42); CHECK(f.d.fault != kIOReturnSuccess);
        CHECK(f.vram[0] == 0 && f.vram[16] == 0); WREG32(f.d, 0, 73); CHECK(f.registers[0] == 0); // terminal fault
    }
    for (bool cancelled : {false, true}) {
        Fixture f; if (cancelled) f.live = false; else f.d.trialEnabled = false;
        WREG32(f.d, 0, 33); CHECK(f.registers[0] == 0 && access_status(f.d) != kIOReturnSuccess);
    }
    { Fixture f; WREG32(f.d, 0x80000 / 4, 51); CHECK(f.d.fault != kIOReturnSuccess); }
    { Fixture f; WBAR0_64(f.d, 132, 51); CHECK(f.d.fault != kIOReturnSuccess && f.vram[16] == 0); }
    { Fixture f; WDOORBELL64(f.d, 4, 51); CHECK(f.d.fault != kIOReturnSuccess && f.doorbell[0] == 0); }
    { Fixture f; WDOORBELL32(f.d, sizeof(f.doorbell), 51); CHECK(f.d.fault != kIOReturnSuccess); }
    { Fixture f; f.d.hdpConfigured = false; amdgpu_hdp_flush(f.d); CHECK(f.d.fault != kIOReturnSuccess); }
    { Fixture f; f.d.vramMcBase = UINT64_MAX - 32; CHECK(f.d.vramMC(128) == UINT64_MAX); }
    { Fixture f; f.d.vramLimit = sizeof(f.vram) + 4; WBAR0_32(f.d, 128, 71); CHECK(f.vram[16] == 0); }
    { Fixture f; bar0_memcpy_to_vram(f.d, 128, nullptr, 4); CHECK(f.d.fault != kIOReturnSuccess); }
    { Fixture f; f.d.ip.setBase(IPBlock::NBIO, 2, 0); CHECK(SOC15_REG_OFFSET_BIDX(f.d, IPBlock::NBIO, 2, 17) == 17); }
    { Fixture f; f.d.ip.set(IPBlock::GC, UINT32_MAX - 1); CHECK(SOC15_REG_OFFSET(f.d, IPBlock::GC, 3) == UINT32_MAX); }
    { Fixture f; CHECK(SOC15_REG_OFFSET_BIDX(f.d, IPBlock::GC, -1, 0) == UINT32_MAX); }
    { Fixture f; WVRAM32_via_mm(f.d, 124, 79); CHECK(f.registers[0] == 0 && f.registers[1] == 0); }
    { Fixture f; WVRAM32_via_mm(f.d, 128, 79); CHECK(f.registers[0] == 0x80000080 && f.registers[1] == 79); }
    { Fixture f; f.registers[1] = 49; CHECK(RVRAM32_via_mm(f.d, 4096) == 49); } // survey read above BAR candidate
    { Fixture f; f.registers[3] = UINT32_MAX; CHECK(!poll_reg(f.d, 3, 1, 1, 0)); }
    { Fixture f; f.registers[3] = 0x80000001; CHECK(poll_psp_response(f.d, 3, 0x80000000, 0x80000000, 0, nullptr) != kIOReturnSuccess); }
    CHECK(fake::liveLocks == 0);
    std::printf("native_compute_access: %u checks, %u failed (RAM only)\n", checks, failed);
    return failed ? 1 : 0;
}
