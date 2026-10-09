// SPDX-License-Identifier: MIT
#include "ExperimentalCompute.hpp"
#include "ComputeSysMem.hpp"
#include "ComputeAccess.hpp"
#include "ipdiscovery.hpp"
#include "psp.hpp"
#include "amd/amdgpu_discovery.h"
#include "amd/amdgpu_gmc.h"
#include "amd/amdgpu_psp.h"
#include "amd/amdgpu_smu.h"
#include "amd/amdgpu_imu.h"
#include "amd/amdgpu_rlc.h"
#include "amd/amdgpu_cp.h"
#include "amd/amdgpu_mes.h"
#include "amd/amdgpu_gfx.h"
#include "amd/amdgpu_doorbell.h"
#include "amd/nbif_v6_3_1.h"
#include "amd/fw_loader.h"
#include "amd/compute_test.h"
#include "fw/fw_table.h"
#include <libkern/c++/OSIterator.h>
#include <libkern/OSAtomic.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <kern/task.h>
using namespace n48compute;
namespace n48native {
struct ExperimentalCompute::Resources final : n48::HwAccess {
    ExperimentalCompute *parent{nullptr}; IOService *owner{nullptr}; IOPCIDevice *pci{nullptr}; IOWorkLoop *loop{nullptr};
    IOMemoryMap *maps[3]{}; IODeviceMemory *descriptors[3]{};
    uint32_t barConfig[5]{};
    IOPMrootDomain *powerRoot{nullptr}; IOPMDriverAssertionID sleepAssertion{kIOPMUndefinedDriverAssertionID};
    DeviceContext dev{}; IpDiscovery discovery; uint8_t discoveryBytes[10240]{};
    GMCContext gmc{}; PSPContext psp{}; IMUContext imu{}; RLCContext rlc{};
    CPContext cp{}; MESContext mes{}; GFXConfig gfx{}; FwLoaderState fw{}; DmaPool pool{};
    bool live() const {
        return !__atomic_load_n(&parent->cancelled_, __ATOMIC_ACQUIRE) && !owner->isInactive() && !pci->isInactive() &&
            owner->getProvider() == pci && pci->isOpen(owner) &&
            pci->extendedConfigRead16(0) == 0x1002 && pci->extendedConfigRead16(2) == 0x7550 &&
            (pci->extendedConfigRead16(4) & 6) == 6 &&
            pci->extendedConfigRead32(0x10) == barConfig[0] && pci->extendedConfigRead32(0x14) == barConfig[1] &&
            pci->extendedConfigRead32(0x18) == barConfig[2] && pci->extendedConfigRead32(0x1c) == barConfig[3] &&
            pci->extendedConfigRead32(0x24) == barConfig[4];
    }
    static bool liveCallback(void *p) { return static_cast<Resources *>(p)->live(); }
    uint32_t regReadIp(uint16_t hw, uint8_t instance, uint8_t seg, uint32_t reg) override {
        uint32_t offset = 0;
        if (!discovery.regByteOffset(hw, instance, seg, reg, offset)) { refuse(dev, kIOReturnBadArgument); return UINT32_MAX; }
        return RREG32(dev, offset / 4);
    }
    bool regWriteIp(uint16_t hw, uint8_t instance, uint8_t seg, uint32_t reg, uint32_t value) override {
        uint32_t offset = 0;
        if (!discovery.regByteOffset(hw, instance, seg, reg, offset)) return refuse(dev, kIOReturnBadArgument);
        WREG32(dev, offset / 4, value); return ready(dev);
    }
    bool vramWrite(uint64_t offset, const void *src, size_t bytes) override {
        bar0_memcpy_to_vram(dev, offset, src, bytes); amdgpu_hdp_flush(dev); return ready(dev);
    }
    bool vramMemset(uint64_t offset, uint8_t value, size_t bytes) override {
        bar0_memset_vram(dev, offset, uint32_t{value} * 0x01010101u, bytes); amdgpu_hdp_flush(dev); return ready(dev);
    }
    ~Resources() {
        // Only used BEFORE the first indirect/register write. Published sessions
        // retain the owner/module and all mappings until cold reboot, never unload.
        if (powerRoot) {
            if (sleepAssertion != kIOPMUndefinedDriverAssertionID) powerRoot->releasePMAssertion(sleepAssertion);
            powerRoot->release();
        }
        for (unsigned i = 3; i-- > 0;) { if (maps[i]) maps[i]->release(); if (descriptors[i]) descriptors[i]->release(); }
        if (dev.indirectLock) IOLockFree(dev.indirectLock);
        if (loop) loop->release(); if (pci) pci->release(); if (owner) owner->release();
    }
};
static void *volatile terminalSession = nullptr;
ExperimentalCompute::~ExperimentalCompute() {
    if (resources_ && !facts_.hardwareTouched) delete resources_;
    // Hardware-visible resources are retained by terminalSession + owner cycle.
}
void ExperimentalCompute::cancel() { __atomic_store_n(&cancelled_, 1, __ATOMIC_RELEASE); }
bool ExperimentalCompute::hardwareTouched() const { return facts_.hardwareTouched; }
ExperimentalCompute::Snapshot ExperimentalCompute::snapshot() const { return facts_; }
IOReturn ExperimentalCompute::run(IOService *owner, IOPCIDevice *pci, IOWorkLoop *loop, const Input &input) {
    if (resources_ || !owner || !pci || !loop || loop->inGate() || owner->getProvider() != pci || !pci->isOpen(owner)) {
        facts_.failedStage = 1; facts_.result = static_cast<uint32_t>(kIOReturnBadArgument);
        return kIOReturnBadArgument;
    }
    auto fail = [&](uint32_t stage, IOReturn result) {
        facts_.failedStage = stage; facts_.result = static_cast<uint32_t>(result);
        if (resources_) {
            facts_.dmaBuffers = resources_->pool.count;
            if (!facts_.hardwareTouched) { delete resources_; resources_ = nullptr; facts_.idleSleepPrevented = false; }
        }
        IOLog("Navi48Native: compute FAILED stage=%u result=0x%x touched=%u; retained until reboot, no retry\n",
              stage, result, facts_.hardwareTouched ? 1u : 0u);
        return result;
    };
    // Explicit scratch must include fixed firmware/table layout + allocation arena.
    if ((input.offset & 0xfffff) || input.bytes < 32 * 1024 * 1024 ||
        !span(input.barBytes[0], input.offset, input.bytes)) return fail(1, kIOReturnBadArgument);
    // x86 PE_Video: bit 0 marks a physical base; lower two bits are flags.
    // No translation/guess for a virtual or unaligned address. Whole allocation,
    // including v_offset/v_length, is protected, not only visible pixels.
    uint64_t base = input.consoleBase;
    if (base & 1) base &= ~uint64_t{3};
    if (!base || (base & 4095) || !input.rowBytes || !input.width || !input.height ||
        !input.depth || input.depth > 64 || (input.depth & 7) || input.width > UINT64_MAX / (input.depth / 8) ||
        input.width * (input.depth / 8) > input.rowBytes || input.height > UINT64_MAX / input.rowBytes)
        return fail(1, kIOReturnBadArgument);
    const uint64_t visible = input.rowBytes * input.height;
    const uint64_t consoleBytes = input.consoleLength ? input.consoleLength : visible;
    if (!span(consoleBytes, input.consoleOffset, visible) || base < input.barPhysical[0] ||
        !span(input.barBytes[0], base - input.barPhysical[0], consoleBytes)) return fail(1, kIOReturnBadArgument);
    const uint64_t consoleOffset = base - input.barPhysical[0];
    if (input.offset < consoleOffset + consoleBytes && consoleOffset < input.offset + input.bytes)
        return fail(1, kIOReturnNotPermitted);
    auto *matching = IOService::serviceMatching("IOAccelerator");
    if (!matching) return fail(1, kIOReturnNoMemory);
    auto *accelerators = IOService::getMatchingServices(matching); matching->release();
    if (!accelerators) return fail(1, kIOReturnNotReady);
    const bool anotherGpu = accelerators->getNextObject() != nullptr; accelerators->release();
    if (anotherGpu) return fail(1, kIOReturnExclusiveAccess);
    resources_ = new Resources;
    if (!resources_) return fail(1, kIOReturnNoMemory);
    auto &r = *resources_; r.parent = this; r.owner = owner; r.pci = pci; r.loop = loop;
    owner->retain(); pci->retain(); loop->retain();
    r.dev.indirectLock = IOLockAlloc(); if (!r.dev.indirectLock) return fail(1, kIOReturnNoMemory);
    constexpr uint8_t regs[3] = {0x10, 0x18, 0x24};
    constexpr IOOptionBits options = kIOMapAnywhere | kIOMapUnique | kIOMapInhibitCache;
    for (unsigned i = 0; i < 3; ++i) {
        auto *md = pci->getDeviceMemoryWithRegister(regs[i]);
        if (!md || md->getLength() != input.barBytes[i]) return fail(1, kIOReturnBadArgument);
        md->retain(); r.descriptors[i] = md; r.maps[i] = md->map(options);
        auto *map = r.maps[i]; IOByteCount bytes = 0;
        if (!map || map->getLength() != input.barBytes[i] || map->getMemoryDescriptor() != md ||
            map->getAddressTask() != kernel_task || !map->getAddress() || (map->getAddress() & 4095) ||
            (map->getMapOptions() & kIOMapReadOnly) || (map->getMapOptions() & kIOMapCacheMask) != kIOMapInhibitCache ||
            map->getPhysicalSegment(0, &bytes, kIOMemoryMapperNone) != input.barPhysical[i] || bytes != input.barBytes[i])
            return fail(1, kIOReturnBadArgument);
    }
    constexpr uint8_t configRegs[5] = {0x10, 0x14, 0x18, 0x1c, 0x24};
    for (unsigned i = 0; i < 5; ++i) r.barConfig[i] = pci->extendedConfigRead32(configRegs[i]);
    if (((uint64_t{r.barConfig[1]} << 32) | (r.barConfig[0] & 0xfffffff0u)) != input.barPhysical[0] ||
        ((uint64_t{r.barConfig[3]} << 32) | (r.barConfig[2] & 0xfffffff0u)) != input.barPhysical[1] ||
        (r.barConfig[4] & 0xfffffff0u) != input.barPhysical[2]) return fail(1, kIOReturnBadArgument);
    r.dev.rmmio = reinterpret_cast<volatile uint32_t *>(r.maps[2]->getAddress()); r.dev.rmmioSize = input.barBytes[2];
    r.dev.bar0 = reinterpret_cast<volatile uint8_t *>(r.maps[0]->getAddress()); r.dev.bar0Size = input.barBytes[0]; r.dev.bar0Phys = input.barPhysical[0];
    r.dev.bar2 = reinterpret_cast<volatile uint8_t *>(r.maps[1]->getAddress()); r.dev.bar2Size = input.barBytes[1]; r.dev.bar2Phys = input.barPhysical[1];
    r.dev.vramBase = input.offset; r.dev.vramLimit = input.offset + input.bytes;
    r.dev.lease = &r; r.dev.leaseAlive = Resources::liveCallback; r.dev.trialEnabled = true;
    r.powerRoot = IOService::getPMRootDomain();
    if (!r.powerRoot) return fail(1, kIOReturnNotReady);
    r.powerRoot->retain();
    r.sleepAssertion = r.powerRoot->createPMAssertion(kIOPMDriverAssertionPreventSystemIdleSleepBit | kIOPMDriverAssertionPreventDisplaySleepBit,
        kIOPMDriverAssertionLevelOn, owner, "Navi48 native compute experiment, reboot-only teardown");
    if (r.sleepAssertion == kIOPMUndefinedDriverAssertionID) return fail(1, kIOReturnNotReady);
    facts_.idleSleepPrevented = true; // DOES NOT block demand/forced sleep
    if (!r.live() || !OSCompareAndSwapPtr(nullptr, &r, &terminalSession)) return fail(1, kIOReturnNotReady);
    // Before FIRST indirect write: keep this owner (and its PCI controller) alive
    // for the rest of the boot. The trial never pretends teardown is qualified.
    facts_.hardwareTouched = true;
    IOLog("Navi48Native: experimental GPU init/compute begins; scratch=0x%llx+0x%llx; no VRAM reservation/restore qualification\n",
          static_cast<unsigned long long>(input.offset), static_cast<unsigned long long>(input.bytes));
    // Fixed bootstrap RCC_CONFIG_MEMSIZE for this exact Navi48 target; discovery
    // below MUST confirm the NBIF segment address before firmware is uploaded.
    const uint32_t mib = RREG32(r.dev, 0x378c / 4);
    if (mib < 256 || mib > 16384 || (mib & 15)) return fail(2, kIOReturnBadArgument);
    r.dev.vramSizeBytes = uint64_t{mib} << 20; facts_.vramBytes = r.dev.vramSizeBytes;
    for (unsigned i = 0; i < sizeof(r.discoveryBytes); i += 4) {
        uint32_t word = RVRAM32_via_mm(r.dev, r.dev.vramSizeBytes - 65536 + i);
        memcpy(r.discoveryBytes + i, &word, 4);
    }
    uint32_t sizeReg = 0;
    if (!ready(r.dev) || !r.discovery.init(r.discoveryBytes, sizeof(r.discoveryBytes)) ||
        !fill_ip_base_table(r.discovery, r.dev.ip) ||
        !r.discovery.regByteOffset(IpDiscovery::HwNbif, 0, 2, 0xc3, sizeReg) || sizeReg != 0x378c ||
        r.dev.ip.getVersion(IPBlock::GC).major != 12 || r.dev.ip.getVersion(IPBlock::GC).minor != 0 ||
        r.dev.ip.getVersion(IPBlock::GC).rev != 1) return fail(2, kIOReturnNoDevice);
    const uint32_t fb = r.regReadIp(IpDiscovery::HwMmhub, 0, 0, 0x554);
    const uint32_t top = r.regReadIp(IpDiscovery::HwMmhub, 0, 0, 0x555);
    const uint32_t fbOffset = r.regReadIp(IpDiscovery::HwMmhub, 0, 0, 0x4c7);
    r.dev.vramMcBase = uint64_t{fb & 0xffffffu} << 24;
    const uint64_t end = (uint64_t{top & 0xffffffu} + 1) << 24;
    if (!ready(r.dev) || fb == UINT32_MAX || top == UINT32_MAX || fbOffset == UINT32_MAX ||
        !r.dev.vramMcBase || end <= r.dev.vramMcBase || end - r.dev.vramMcBase < r.dev.vramSizeBytes ||
        !span(r.dev.vramSizeBytes, input.offset, input.bytes)) return fail(2, kIOReturnBadArgument);
    facts_.mcBase = r.dev.vramMcBase; facts_.stage = 2;
    r.pool.owner = owner; r.pool.pci = pci; r.pool.loop = loop;
    if (!sysmem_set_pool(&r.pool)) return fail(3, kIOReturnNotReady);
    // Ordered, error-propagating ladder. No non-fatal MES/SMU/GART bypasses.
    auto step = [&](uint32_t stage, IOReturn result) {
        IOLog("Navi48Native: GPU stage=%u result=0x%x access=0x%x\n", stage, result, r.dev.fault);
        if (result != kIOReturnSuccess || !ready(r.dev)) return fail(stage, result != kIOReturnSuccess ? result : kIOReturnIOError);
        facts_.stage = stage; return kIOReturnSuccess;
    };
    IOReturn kr = step(3, nbif_v6_3_1_doorbell_path_init(r.dev, r.dev.bar2Phys));
    if (kr) return kr;
    if (nbif_v6_3_1_read_hdp_remap(r.dev) != 0x7f000) return fail(3, kIOReturnIOError);
    r.dev.hdpConfigured = true;
    // Verify BAR0 origin against the MC/MM window using unique data, then restore
    // scratch words. A matching zero-filled read is not accepted as this proof.
    const uint64_t probe = input.offset + 0x7e0000; uint32_t saved[8]{};
    for (unsigned i = 0; i < 8; ++i) saved[i] = RBAR0_32(r.dev, probe + i * 4);
    for (unsigned i = 0; i < 8; ++i) WBAR0_32(r.dev, probe + i * 4, 0x90704800u + i);
    amdgpu_hdp_flush(r.dev); bool originOk = true;
    for (unsigned i = 0; i < 8; ++i) if (RVRAM32_via_mm(r.dev, probe + i * 4) != 0x90704800u + i) originOk = false;
    bar0_memcpy_to_vram(r.dev, probe, saved, sizeof(saved)); amdgpu_hdp_flush(r.dev);
    if (!originOk || !ready(r.dev)) return fail(3, kIOReturnIOError);
    n48::PspLoader boot(r);
    const auto *sos = fw_get(FwId::PSP_SOS);
    if (!sos || !boot.parseContainer(sos->data, sos->size) || !boot.setFwPriOffset(input.offset) ||
        !boot.preflight() || boot.mcBase() != r.dev.vramMcBase || !boot.runChain()) return fail(4, kIOReturnIOError);
    facts_.stage = 4;
    r.gmc.skipDisplayRisky = true; // preserve VBIOS MMHUB cache/AGP/TLB model
    if ((kr = step(5, gmc_init(r.dev, r.gmc)))) return kr;
    if (r.gmc.vram_start != facts_.mcBase || r.gmc.real_vram_size != facts_.vramBytes ||
        r.dev.vramMcBase != facts_.mcBase) return fail(5, kIOReturnIOError);
    if ((kr = step(6, psp_init(r.dev, r.psp)))) return kr;
    if ((kr = step(6, psp_parse_sos_microcode(r.psp, sos->data, sos->size)))) return kr;
    if ((kr = step(6, psp_adopt_sos_alive(r.dev, r.psp)))) return kr;
    if ((kr = step(7, psp_ring_create(r.dev, r.psp)))) return kr;
    if ((kr = step(7, psp_query_fw_reservation(r.dev, r.psp)))) return kr;
    if ((kr = step(8, psp_setup_tmr(r.dev, r.psp)))) return kr;
    if ((kr = step(8, psp_rl_load(r.dev, r.psp)))) return kr;
    if ((kr = step(9, fw_loader_prepare(r.dev, r.psp, r.fw)))) return kr;
    if ((kr = step(9, psp_load_non_psp_fw(r.dev, r.psp, fw_loader_make(r.fw))))) return kr;
    facts_.firmwareLoaded = true; r.imu.microcode_loaded = r.fw.imuPresent; r.rlc.microcode_loaded = r.fw.rlcPresent;
    if ((kr = step(10, smu_hw_init(r.dev, true)))) return kr;
    if ((kr = step(11, imu_init_full(r.dev, r.imu)))) return kr;
    if ((kr = step(12, rlc_init_full(r.dev, r.gmc, r.rlc)))) return kr;
    if ((kr = step(13, doorbell_init(r.dev, r.dev.doorbell)))) return kr;
    if ((kr = step(13, gmc_gfxhub_gart_enable(r.dev, r.gmc)))) return kr;
    const uint64_t *t = r.psp.tmr_fw_addr_by_type;
    r.cp.tmr_pfp_ic = t[PSPGfxFwType::RS64_PFP]; r.cp.tmr_pfp_dc = t[PSPGfxFwType::RS64_PFP_P0];
    r.cp.tmr_me_ic = t[PSPGfxFwType::RS64_ME]; r.cp.tmr_me_dc = t[PSPGfxFwType::RS64_ME_P0];
    r.cp.tmr_mec_ic = t[PSPGfxFwType::RS64_MEC]; r.cp.tmr_mec_dc0 = t[PSPGfxFwType::RS64_MEC_P0]; r.cp.tmr_mec_dc1 = t[PSPGfxFwType::RS64_MEC_P1];
    if ((kr = step(14, cp_init_full(r.dev, r.gmc, r.cp, false)))) return kr;
    if ((kr = step(15, mes_init_full(r.dev, r.psp, r.gmc, r.mes)))) return kr;
    if ((kr = step(16, gfx_constants_init(r.dev, r.gfx)))) return kr;
    if ((kr = step(16, cp_gfx_mqd_init(r.dev, r.cp)))) return kr;
    if ((kr = step(16, cp_map_gfx_kgq_mes(r.dev, r.cp, r.mes)))) return kr;
    if ((kr = step(16, cp_gfx_start(r.dev, r.cp, false)))) return kr;
    if ((kr = step(16, cp_ring_test_scratch(r.dev, r.cp, 1000000)))) return kr;
    if ((kr = step(16, cp_submit_eop_test(r.dev, r.cp, 2000000, nullptr)))) return kr;
    facts_.initialized = true;
    for (unsigned variant = 0; variant < 2; ++variant) {
        ComputeTestResult result{};
        kr = compute_dispatch_test(r.dev, r.gmc, r.cp, &result, variant != 0);
        facts_.elapsedUs[variant] = result.elapsed_us;
        facts_.fenceLanded[variant] = result.fence_landed; facts_.ibTestPassed[variant] = result.ib_test_passed;
        if (result.fence_landed && result.ib_test_passed) { // do not count upstream's prefilled 32 before execution
            facts_.lanesChecked += result.lanes_checked; facts_.lanesWrong += result.lanes_mismatched;
        }
        for (unsigned i = 0; i < 4; ++i) { facts_.observed[variant][i] = result.observed[i]; facts_.expected[variant][i] = result.expected[i]; }
        if (kr || !result.fence_landed || !result.ib_test_passed || result.lanes_checked != 32 || result.lanes_mismatched)
            return fail(17, kr ? kr : kIOReturnIOError);
        if ((kr = step(17, kr))) return kr;
    }
    facts_.computePassed = true; // actual fences + all 64 comparisons already passed
    // Return clocks to the upstream low state after the two one-shot dispatches.
    if ((kr = step(18, smu_set_power_state(r.dev, SMUPowerState::Low)))) return kr;
    facts_.dmaBuffers = r.pool.count; facts_.result = 0;
    IOLog("Navi48Native: COMPUTE PASSED two gfx1201 shaders, 64 lanes verified, fences landed; no Metal/WindowServer integration\n");
    return kIOReturnSuccess;
}
}
