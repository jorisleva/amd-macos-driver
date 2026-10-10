// SPDX-License-Identifier: MIT
// Targeted smoke test: the real IOService owns/withdraws the real controller.
// No core PSP routine is linked into this process; all device APIs are doubles.
#include "Navi48Native.hpp"
#include <cstdio>
#include <thread>

namespace fakecompute { extern unsigned calls; extern bool simulatePublication; extern void (*onRun)(); }
static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
void IOLog(const char *, ...) {}
void IOSleep(unsigned) { CHECK(false); } // no polling/firmware path belongs to this smoke test
constexpr uint64_t MiB = 1024 * 1024;
constexpr unsigned reg[3] = {0x10, 0x18, 0x24};
constexpr IOOptionBits options = kIOMapAnywhere | kIOMapUnique | kIOMapReadOnly | kIOMapInhibitCache;
static n48native::MapCheck expected(unsigned bar, unsigned which) {
    // NullMap is only for a missing map object; the 12 field mutations below
    // keep the object and diverge one observed property each.
    constexpr n48native::MapCheck table[3][12] = {
        {n48native::MapCheck::NullAddress, n48native::MapCheck::UnalignedAddress,
         n48native::MapCheck::RangeOverflow, n48native::MapCheck::LengthMismatch,
         n48native::MapCheck::LengthMismatch, n48native::MapCheck::ContiguousMismatch,
         n48native::MapCheck::PhysicalMismatch, n48native::MapCheck::CacheMismatch,
         n48native::MapCheck::FlagsMismatch, n48native::MapCheck::FlagsMismatch,
         n48native::MapCheck::TaskMismatch, n48native::MapCheck::DescriptorMismatch},
        {n48native::MapCheck::NullAddress, n48native::MapCheck::UnalignedAddress,
         n48native::MapCheck::RangeOverflow, n48native::MapCheck::LengthMismatch,
         n48native::MapCheck::LengthMismatch, n48native::MapCheck::ContiguousMismatch,
         n48native::MapCheck::PhysicalMismatch, n48native::MapCheck::CacheMismatch,
         n48native::MapCheck::FlagsMismatch, n48native::MapCheck::FlagsMismatch,
         n48native::MapCheck::TaskMismatch, n48native::MapCheck::DescriptorMismatch},
        {n48native::MapCheck::NullAddress, n48native::MapCheck::UnalignedAddress,
         n48native::MapCheck::RangeOverflow, n48native::MapCheck::LengthMismatch,
         n48native::MapCheck::LengthMismatch, n48native::MapCheck::ContiguousMismatch,
         n48native::MapCheck::PhysicalMismatch, n48native::MapCheck::CacheMismatch,
         n48native::MapCheck::FlagsMismatch, n48native::MapCheck::FlagsMismatch,
         n48native::MapCheck::TaskMismatch, n48native::MapCheck::DescriptorMismatch}};
    return table[bar][which];
}
struct Fixture {
    IOPCIDevice pci;
    IOPlatformExpert platform;
    Navi48Native driver;
    IODeviceMemory *descriptors[3]{};
    bool keepDiagnostic{false};
    Fixture() {
        fake::clearBootDiagnostic();
        fake::bootPresent = true; fake::bootValue = 1; fake::offsetPresent = true; fake::bytesPresent = true;
        fake::scratchOffset = 64 * MiB; fake::scratchBytes = 24 * MiB;
        fake::computePresent = fake::riskPresent = false; fake::computeValue = fake::riskValue = 0;
        fakecompute::onRun = nullptr; fakecompute::simulatePublication = false;
        fake::allocationBudget = -1; fake::baseStart = true; fake::lockFails = false; fake::events.clear();
        fake::error = fake::Error::None; fake::callback = nullptr; fake::afterGenerate = nullptr;
        fake::deviceMapper = true; fake::discontiguous = false;
        driver.provider = &pci; fake::platform = &platform;
        pci.set16(0x00, 0x1002); pci.set16(0x02, 0x7550); pci.set16(0x2c, 0x1849); pci.set16(0x2e, 0x5417);
        pci.set32(0x08, 0x030000c0); pci.set16(0x04, 7);
        constexpr uint64_t base[3] = {0x440000000ULL, 0x450000000ULL, 0xfcb00000ULL};
        constexpr uint64_t size[3] = {256 * MiB, 2 * MiB, 512 * 1024};
        for (unsigned i = 0; i < 3; ++i) {
            auto *d = new IODeviceMemory; descriptors[i] = d; pci.descriptors[i] = d; d->index = i;
            d->bytes = size[i]; d->contiguous = size[i]; d->physical = base[i];
            d->recipe = {false, 0x1000000000ULL + i * 0x100000000ULL, base[i], size[i], size[i], options, kernel_task, d};
            pci.set32(reg[i], static_cast<uint32_t>(base[i]) | (i < 2 ? 0xc : 0));
            if (i < 2) pci.set32(reg[i] + 4, static_cast<uint32_t>(base[i] >> 32));
        }
        platform.video.v_baseAddr = base[0]; platform.video.v_rowBytes = 4096;
        platform.video.v_width = 1024; platform.video.v_height = 256; platform.video.v_depth = 32;
    }
    ~Fixture() {
        fake::callback = nullptr; fake::afterGenerate = nullptr;
        driver.free();
        if (!keepDiagnostic) fake::clearBootDiagnostic();
        for (auto *d : descriptors) d->release();
        fake::platform = nullptr;
    }
    void init() { CHECK(driver.init()); }
    void closed() {
        CHECK(pci.client == nullptr && pci.references() == 1 && driver.references() == 1);
        CHECK(fake::mapsMade == fake::mapsFreed);
        CHECK(!driver.getProperty("Navi48Native,Resources"));
        CHECK(driver.registrations == 0 && pci.propertyWrites == 0);
    }
};
// Published-model fixtures intentionally remain reachable, like a boot-long
// terminal lease. Never reinterpret this lifetime model as hardware success.
static std::vector<Fixture *> terminalFixtures;
static Navi48Native *callbackDriver;
static IOPCIDevice *callbackProvider;
static uint64_t value(OSDictionary *dictionary, const char *key) {
    auto *entry = dynamic_cast<OSNumber *>(dictionary ? dictionary->getObject(key) : nullptr);
    CHECK(entry != nullptr);
    return entry ? entry->unsigned64BitValue() : UINT64_MAX;
}
int main() {
    // OFF/missing placement fails before the first PCI operation.
    for (unsigned which = 0; which < 5; ++which) {
        Fixture f; f.init();
        if (which == 0) fake::bootPresent = false;
        if (which == 1) fake::bootValue = 0;
        if (which == 2) fake::offsetPresent = false;
        if (which == 3) fake::scratchOffset += 4096;
        if (which == 4) fake::scratchBytes = 4 * MiB;
        CHECK(f.driver.probe(&f.pci, nullptr) == nullptr);
        CHECK(!f.driver.start(&f.pci));
        CHECK(f.pci.opens == 0 && f.pci.configReads == 0 && f.driver.starts == 0);
        f.closed();
    }
    // Complete opt-in: driver/controller integration, not a simulated GPU ACK.
    {
        Fixture f; f.init(); CHECK(f.driver.probe(&f.pci, nullptr) == &f.driver);
        CHECK(f.driver.start(&f.pci));
        auto *report = dynamic_cast<OSDictionary *>(f.driver.getProperty("Navi48Native,Resources"));
        CHECK(value(report, "MappingsHeld") == 1 && value(report, "ObservationsValid") == 1);
        CHECK(value(report, "Blockers") == n48native::kUnimplementedHardwareProofs);
        CHECK(value(report, "GPUInitialized") == 0 && value(report, "FirmwareExecuted") == 0 && value(report, "AccessEnabled") == 0);
        CHECK(value(report, "SchemaVersion") == 2 && value(report, "DMAAllocatorInvoked") == 1);
        CHECK(value(report, "DMAPhase") == static_cast<uint32_t>(n48native::DmaBuffer::Phase::Prepared));
        CHECK(value(report, "DMAResult") == kIOReturnSuccess);
        CHECK(value(report, "DMABytes") == 65536 && value(report, "DMAPages") == 16);
        CHECK(value(report, "DMADeviceMapper") == 1 && value(report, "DMAAddressPublished") == 0 && value(report, "GPUDMAValidated") == 0);
        auto *bar = dynamic_cast<OSDictionary *>(report ? report->getObject("BAR0") : nullptr);
        CHECK(value(bar, "CPUPhysical") == 0x440000000ULL);
        IOUserClient *client = reinterpret_cast<IOUserClient *>(uintptr_t{1});
        CHECK(f.driver.newUserClient(nullptr, nullptr, 0, &client) == kIOReturnUnsupported && !client);
        client = reinterpret_cast<IOUserClient *>(uintptr_t{1});
        CHECK(f.driver.newUserClient(nullptr, nullptr, 0, nullptr, &client) == kIOReturnUnsupported && !client);
        callbackDriver = &f.driver; callbackProvider = &f.pci;
        f.pci.afterClose = [](IOPCIDevice *) { callbackDriver->stop(callbackProvider); };
        f.driver.stop(&f.pci); f.closed(); CHECK(f.driver.stops == 1);
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(diagnostic, "Checkpoint") == 14 && value(diagnostic, "DriverVersion") == 0x209);
        CHECK(value(diagnostic, "StartReturn") == 0 && value(diagnostic, "DMAObserved") == 1);
        CHECK(value(diagnostic, "ComputeObserved") == 0);
        CHECK(!f.driver.start(&f.pci)); // no hot restart/rearm
        CHECK(fake::bootDiagnostic == diagnostic); // original result not overwritten by rejected rearm
    }
    // Hardware trial requires BOTH explicit args. No call with partial/invalid opt-in.
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        Fixture f; f.init();
        fake::computePresent = true; fake::riskPresent = true;
        fake::computeValue = invalid == 0 ? 0 : 1; fake::riskValue = invalid == 1 ? 0 : 1;
        if (invalid == 2) fake::computeValue = 2;
        if (invalid == 3) fake::riskPresent = false;
        const auto calls = fakecompute::calls;
        CHECK(!f.driver.start(&f.pci)); CHECK(fakecompute::calls == calls);
        CHECK(f.pci.opens == 0 && f.driver.starts == 0); f.closed();
    }
    // The REAL service calls the lifecycle double outside its gate. Double
    // reports a pre-write failure, NOT a simulated successful shader/Radeon.
    for (bool stopDuring : {false, true}) {
        Fixture f; f.init(); fake::scratchBytes = 64 * MiB;
        fake::computePresent = fake::riskPresent = true; fake::computeValue = fake::riskValue = 1;
        callbackDriver = &f.driver; callbackProvider = &f.pci;
        fakecompute::onRun = stopDuring ? +[]() {
            CHECK(!callbackDriver->getWorkLoop()->inGate());
            CHECK(callbackProvider->client == callbackDriver);
            std::thread stop([]() { callbackDriver->stop(callbackProvider); }); stop.join();
        } : nullptr;
        const auto calls = fakecompute::calls;
        CHECK(!f.driver.start(&f.pci)); CHECK(fakecompute::calls == calls + 1);
        auto *report = dynamic_cast<OSDictionary *>(f.driver.getProperty("Navi48Native,Compute"));
        CHECK(value(report, "HardwareTouched") == 0 && value(report, "ComputePassed") == 0);
        CHECK(value(report, "FailedStage") == 1);
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(diagnostic, "Checkpoint") == 13 && value(diagnostic, "ComputeObserved") == 1);
        CHECK(value(diagnostic, "FailedStage") == 1 && value(diagnostic, "PreflightCheck") == 3);
        CHECK(value(diagnostic, "HardwareTouched") == 0 && value(diagnostic, "ComputePassed") == 0);
        // The lifecycle double fails before RW mappings: all RW bars stay NotChecked.
        auto *rwBar0 = dynamic_cast<OSDictionary *>(diagnostic->getObject("RWBar0Map"));
        CHECK(value(rwBar0, "MapCheck") == static_cast<uint32_t>(n48native::ExperimentalCompute::RwMapCheck::NotChecked));
        CHECK(value(rwBar0, "ObservedAddress") == 0);
        CHECK(value(rwBar0, "Origin") == static_cast<uint32_t>(n48native::ExperimentalCompute::RwOrigin::Retained));
        CHECK(value(report, "HardwareQualificationComplete") == 0 && value(report, "MetalAcceleration") == 0);
        f.closed(); CHECK(f.driver.stops == 1);
        fakecompute::onRun = nullptr;
    }
    // Exact PCI target and partial acquisition propagate failure through start.
    for (bool wrongCard : {false, true}) {
        Fixture f; f.init();
        if (wrongCard) f.pci.set16(0x02, 0x7551);
        else f.descriptors[1]->recipe.fail = true;
        CHECK(!f.driver.start(&f.pci)); f.closed(); CHECK(f.driver.starts == 1 && f.driver.stops == 1);
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(diagnostic, "Checkpoint") == 10 && value(diagnostic, "PlatformObserved") == 1);
        CHECK(value(diagnostic, "PlatformResult") == static_cast<uint32_t>(wrongCard ? n48native::PlatformResult::WrongCard : n48native::PlatformResult::MapFailed));
        CHECK(value(diagnostic, "FailedBar") == (wrongCard ? 0 : 0x18));
        CHECK(value(diagnostic, "DMAObserved") == 0 && value(diagnostic, "ComputeObserved") == 0);
        auto *bar = dynamic_cast<OSDictionary *>(diagnostic->getObject(wrongCard ? "BAR0Map" : "BAR2Map"));
        CHECK(value(bar, "MapCheck") == static_cast<uint32_t>(n48native::MapCheck::NotChecked));
        CHECK(value(bar, "ObservedAddress") == 0 && value(bar, "ObservedLength") == 0);
    }
    // Stop delivered recursively while acquiring cancels, rather than deleting
    // the controller while its own lock is held.
    {
        Fixture f; f.init(); callbackDriver = &f.driver; callbackProvider = &f.pci;
        f.descriptors[0]->afterMap = [](IOMemoryDescriptor *) { callbackDriver->stop(callbackProvider); };
        CHECK(!f.driver.start(&f.pci)); f.closed(); CHECK(f.driver.stops == 1);
    }
    // Failed publication unwinds the lease; no stale registry status.
    {
        Fixture f; f.init();
        f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { fake::allocationBudget = 0; };
        CHECK(!f.driver.start(&f.pci)); f.closed(); CHECK(f.driver.stops == 1);
        fake::allocationBudget = -1;
    }
    // Blocking prepare is outside the gate; stop on ANOTHER thread returns
    // without withdrawing the PCI lease until allocation exits and unwinds.
    {
        Fixture f; f.init(); callbackDriver = &f.driver; callbackProvider = &f.pci;
        const auto allocations = fake::allocations, clears = fake::clears, completes = fake::completes;
        fake::callback = []() {
            CHECK(!callbackDriver->getWorkLoop()->inGate());
            CHECK(callbackProvider->client == callbackDriver);
            std::thread stop([]() { callbackDriver->stop(callbackProvider); }); stop.join();
        };
        CHECK(!f.driver.start(&f.pci));
        CHECK(fake::allocations == allocations + 1 && fake::clears == clears + 1 && fake::completes == completes + 1);
        f.closed(); CHECK(f.driver.stops == 1);
    }
    // Prepare failures unwind outside the gate, before closing the provider.
    for (auto error : {fake::Error::Allocate, fake::Error::MemoryPrepare, fake::Error::Command,
                       fake::Error::Prepare, fake::Error::Generate, fake::Error::Alias}) {
        Fixture f; f.init(); callbackDriver = &f.driver; callbackProvider = &f.pci;
        fake::error = error;
        fake::callback = []() {
            CHECK(!callbackDriver->getWorkLoop()->inGate());
            CHECK(callbackProvider->client == callbackDriver);
        };
        CHECK(!f.driver.start(&f.pci)); f.closed(); CHECK(f.driver.stops == 1);
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(diagnostic, "Checkpoint") == 11 && value(diagnostic, "DMAObserved") == 1);
        CHECK(value(diagnostic, "DMAResult") != 0 && value(diagnostic, "ComputeObserved") == 0);
    }
    // Configuration/console can change while the gate is released. Revalidation
    // invalidates the controller but holds its lease until DMA cleanup finishes.
    for (bool console : {false, true}) {
        Fixture f; f.init(); callbackDriver = &f.driver; callbackProvider = &f.pci;
        const auto clears = fake::clears, completes = fake::completes;
        if (console) fake::afterGenerate = []() { ++fake::platform->video.v_height; };
        else fake::afterGenerate = []() { callbackProvider->set16(0x04, 3); };
        fake::callback = []() {
            CHECK(!callbackDriver->getWorkLoop()->inGate());
            CHECK(callbackProvider->client == callbackDriver);
        };
        CHECK(!f.driver.start(&f.pci));
        CHECK(fake::clears == clears + 1 && fake::completes == completes + 1);
        f.closed(); CHECK(f.driver.stops == 1);
    }
    // Final publication failure also cleans the actual DMA allocation.
    {
        Fixture f; f.init(); const auto clears = fake::clears;
        fake::afterGenerate = []() { fake::allocationBudget = 0; };
        CHECK(!f.driver.start(&f.pci));
        CHECK(fake::clears == clears + 1); f.closed(); CHECK(f.driver.stops == 1);
        fake::allocationBudget = -1;
    }
    // Default mapper + discontiguous page list are reported, never labelled GPU
    // qualified. A complete service suspension drains RAM before closing PCI.
    {
        Fixture f; f.init(); fake::deviceMapper = false; fake::discontiguous = true;
        CHECK(f.driver.start(&f.pci));
        auto *report = dynamic_cast<OSDictionary *>(f.driver.getProperty("Navi48Native,Resources"));
        CHECK(value(report, "DMADeviceMapper") == 0 && value(report, "GPUDMAValidated") == 0);
        callbackDriver = &f.driver; callbackProvider = &f.pci;
        fake::callback = []() {
            CHECK(!callbackDriver->getWorkLoop()->inGate());
            CHECK(callbackProvider->client == callbackDriver);
        };
        CHECK(f.driver.message(kIOMessageServiceIsSuspended, &f.pci) == kIOReturnSuccess);
        f.closed(); f.driver.stop(&f.pci); CHECK(f.driver.stops == 1);
    }
    // Provider termination invalidates mappings, then base stop happens once.
    {
        Fixture f; f.init(); CHECK(f.driver.start(&f.pci));
        CHECK(f.driver.willTerminate(&f.pci, 0)); f.closed();
        f.driver.stop(&f.pci); CHECK(f.driver.stops == 1);
    }
    // Partial IOService initialization can be freed without an event source leak.
    for (int budget : {0, 1}) {
        Fixture f; fake::allocationBudget = budget;
        CHECK(!f.driver.init()); f.closed(); fake::allocationBudget = -1;
    }
    // Descriptor-identity matrix through the REAL service path. Same guard
    // ordering as the controller suite: a persistent substitution surfaces as
    // ConfigurationChanged; only a shared map declaring another object
    // reaches checkMaps() with reread==retained. The afterMap hook flips the
    // provider phase exactly when mapping begins, after checkDescriptors().
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        Fixture f; f.init();
        auto *other = new IODeviceMemory; other->index = 9;
        f.descriptors[0]->recipe.backing = scenario == 0 ? f.descriptors[1] : other;
        // afterMap is a plain function pointer (no captures): route through
        // the live fixture pointer, cleared in the fixture destructor path.
        // start() runs synchronously inside this iteration, so the pointer
        // cannot dangle past f.closed().
        if (scenario == 0) {
            f.pci.substitute[0] = f.descriptors[1];
            callbackProvider = &f.pci;
            f.descriptors[0]->afterMap = [](IOMemoryDescriptor *) {
                callbackProvider->mappingPhase = true;
            };
        }
        CHECK(!f.driver.start(&f.pci));
        f.closed();
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(diagnostic, "Checkpoint") == 10 && value(diagnostic, "FailedBar") == 0x10);
        if (scenario == 0) {
            // Persistent substitution trips the earlier descriptor guard.
            CHECK(value(diagnostic, "PlatformResult") == static_cast<uint32_t>(n48native::PlatformResult::ConfigurationChanged));
            CHECK(value(diagnostic, "PlatformDecision") == static_cast<uint32_t>(n48native::PlatformResult::ConfigurationChanged));
        } else {
            // Shared map declaring a NON-conforming object (zero geometry):
            // adoption refused, checkMaps() still names the mismatch.
            auto *rejected = dynamic_cast<OSDictionary *>(diagnostic->getObject("BAR0Map"));
            CHECK(value(diagnostic, "PlatformResult") == static_cast<uint32_t>(n48native::PlatformResult::InvalidMap));
            CHECK(value(rejected, "MapCheck") == static_cast<uint32_t>(n48native::MapCheck::DescriptorMismatch));
            CHECK(value(rejected, "DescriptorMatch") == 0);
            CHECK(value(rejected, "RereadPresent") == 1 && value(rejected, "RereadMatch") == 1);
            CHECK(value(rejected, "DeclaredIsReread") == 0);
            CHECK(value(rejected, "DescriptorOrigin") == static_cast<uint32_t>(n48native::DescriptorOrigin::NotAdopted));
        }
        other->release();
    }
    // Way 1 through the REAL service path: a shared mapping declaring a
    // FULLY conforming object is adopted after revalidation, and start()
    // proceeds past acquisition (it then continues to DMA, as before).
    {
        Fixture f; f.init();
        constexpr uint64_t sizes[3] = {256 * MiB, 2 * MiB, 512 * 1024};
        IODeviceMemory *adopted[3]{};
        for (unsigned i = 0; i < 3; ++i) {
            adopted[i] = new IODeviceMemory; adopted[i]->index = 9 + i;
            adopted[i]->bytes = sizes[i]; adopted[i]->contiguous = sizes[i];
            adopted[i]->physical = f.descriptors[i]->physical;
            f.descriptors[i]->recipe.backing = adopted[i];
        }
        CHECK(f.driver.probe(&f.pci, nullptr) == &f.driver);
        CHECK(f.driver.start(&f.pci)); // acquisition adopted; DMA proceeds
        auto *report = dynamic_cast<OSDictionary *>(f.driver.getProperty("Navi48Native,Resources"));
        CHECK(value(report, "MappingsHeld") == 1 && value(report, "ObservationsValid") == 1);
        auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        auto *bar0 = dynamic_cast<OSDictionary *>(diagnostic->getObject("BAR0Map"));
        CHECK(value(bar0, "MapCheck") == static_cast<uint32_t>(n48native::MapCheck::Ok));
        CHECK(value(bar0, "DescriptorOrigin") == static_cast<uint32_t>(n48native::DescriptorOrigin::DeclaredAdopted));
        CHECK(value(bar0, "DescriptorMatch") == 1 && value(bar0, "RereadMatch") == 1);
        CHECK(value(bar0, "DeclaredIsReread") == 0);
        callbackDriver = &f.driver; callbackProvider = &f.pci;
        f.pci.afterClose = [](IOPCIDevice *) { callbackDriver->stop(callbackProvider); };
        f.driver.stop(&f.pci); f.closed();
        for (auto *d : adopted) d->release();
    }
    // Each rejected BAR mapping field keeps its decoded ID in the persistent
    // diagnostic. Uses the exact InvalidMap recipe coverage from the controller
    // suite, so the next boot names the BAR0 property instead of a bare code.
    for (unsigned bar = 0; bar < 3; ++bar) {
        for (unsigned which = 0; which < 12; ++which) {
            Fixture f; f.init(); auto &recipe = f.descriptors[bar]->recipe;
            if (which == 0) recipe.address = 0;
            if (which == 1) ++recipe.address;
            if (which == 2) recipe.address = UINT64_MAX - 4095;
            if (which == 3) --recipe.bytes;
            if (which == 4) ++recipe.bytes;
            if (which == 5) --recipe.contiguous;
            if (which == 6) ++recipe.physical;
            if (which == 7) recipe.options = (options & ~kIOMapCacheMask) | 0x400;
            if (which == 8) recipe.options &= ~kIOMapReadOnly;
            if (which == 9) recipe.options &= ~kIOMapUnique;
            if (which == 10) recipe.task = nullptr;
            if (which == 11) recipe.backing = f.descriptors[(bar + 1) % 3];
            // The mappingPhase stub from the identity matrix above must not
            // leak into this loop: each fixture starts unsubstituted.
            CHECK(!f.pci.mappingPhase);
            CHECK(!f.driver.start(&f.pci)); f.closed();
            auto *diagnostic = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
            CHECK(value(diagnostic, "Checkpoint") == 10 && value(diagnostic, "FailedBar") == reg[bar]);
            constexpr const char *names[3] = {"BAR0Map", "BAR2Map", "BAR5Map"};
            auto *rejected = dynamic_cast<OSDictionary *>(diagnostic->getObject(names[bar]));
            CHECK(value(rejected, "MapCheck") == static_cast<uint32_t>(expected(bar, which)));
            CHECK(value(rejected, "ObservedAddress") == recipe.address || which == 11);
            CHECK(value(rejected, "RereadPresent") == 1 && value(rejected, "RereadMatch") == 1);
            // which==11 declares another object while the provider still
            // returns the retained one: DeclaredIsReread==0, the shared-map
            // scenario. All other mutations keep declared==reread.
            CHECK(value(rejected, "DeclaredIsReread") == (which == 11 ? 0 : 1));
            // Bars before the failure are Ok; bars after it were never reached.
            // BAR0 fails first, so BAR2/BAR5 stay NotChecked; BAR5 fails last,
            // so BAR0/BAR2 are already Ok.
            if (bar < 2) {
                auto *unreached = dynamic_cast<OSDictionary *>(diagnostic->getObject(names[bar + 1]));
                CHECK(value(unreached, "MapCheck") == static_cast<uint32_t>(n48native::MapCheck::NotChecked));
                CHECK(value(unreached, "ObservedAddress") == 0);
            } else {
                auto *earlier = dynamic_cast<OSDictionary *>(diagnostic->getObject(names[0]));
                CHECK(value(earlier, "MapCheck") == static_cast<uint32_t>(n48native::MapCheck::Ok));
                CHECK(value(earlier, "ObservedAddress") != 0);
            }
        }
    }
    CHECK(fake::liveObjects == 0 && fake::liveLocks == 0 && fake::mapsMade == fake::mapsFreed);
    CHECK(!fake::resourcePublishedInGate); // publication after gate and cleanup
    // Dictionary survives real service destruction, WITHOUT keeping its owner
    // or PCI lease alive. No fabricated GPU success in this lifecycle model.
    {
        { Fixture f; f.keepDiagnostic = true; f.init(); f.pci.set16(0x02, 0x7551);
          CHECK(!f.driver.start(&f.pci)); f.closed(); }
        auto *held = dynamic_cast<OSDictionary *>(fake::bootDiagnostic);
        CHECK(value(held, "Checkpoint") == 10 && value(held, "StartReturn") != 0);
        CHECK(value(held, "PlatformResult") == static_cast<uint32_t>(n48native::PlatformResult::WrongCard));
        CHECK(value(held, "ComputeObserved") == 0);
        fake::clearBootDiagnostic();
    }
    CHECK(fake::liveObjects == 0);
    // GPU-visible lifetime MODEL: even a failed trial or failed publication must
    // retain PCI/mappings/DMA and module owner, outside all stop callbacks.
    for (bool publicationFails : {false, true}) {
        auto *f = new Fixture; terminalFixtures.push_back(f); f->init();
        fake::scratchBytes = 64 * MiB; fake::computePresent = fake::riskPresent = true;
        fake::computeValue = fake::riskValue = 1; fakecompute::simulatePublication = true;
        if (publicationFails) fakecompute::onRun = []() { fake::allocationBudget = 0; };
        const auto closes = f->pci.closes, clears = fake::clears, freed = fake::mapsFreed;
        CHECK(f->driver.start(&f->pci)); CHECK(f->driver.registrations == 1);
        if (!publicationFails) {
            auto *report = dynamic_cast<OSDictionary *>(f->driver.getProperty("Navi48Native,Compute"));
            CHECK(value(report, "ResourcesRetainedUntilReboot") == 1);
            CHECK(value(report, "GPUInitialized") == 0 && value(report, "ComputePassed") == 0);
        }
        fake::allocationBudget = -1; fakecompute::onRun = nullptr;
        CHECK(f->driver.message(kIOMessageServiceIsSuspended, &f->pci) == kIOReturnSuccess);
        CHECK(f->pci.closes == closes && fake::clears == clears && fake::mapsFreed == freed);
        CHECK(f->pci.client == &f->driver && f->driver.references() > 1 && f->driver.stops == 0);
        f->driver.stop(&f->pci); CHECK(f->driver.stops == 1);
        CHECK(!f->driver.start(&f->pci));
    }
    std::printf("native_kext_smoke: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
