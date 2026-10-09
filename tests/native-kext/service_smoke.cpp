// SPDX-License-Identifier: MIT
// Targeted smoke test: the real IOService owns/withdraws the real controller.
// No core PSP routine is linked into this process; all device APIs are doubles.
#include "Navi48Native.hpp"
#include <cstdio>

static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
void IOLog(const char *, ...) {}
void IOSleep(unsigned) { CHECK(false); } // no polling/firmware path belongs to this smoke test
constexpr uint64_t MiB = 1024 * 1024;
struct Fixture {
    IOPCIDevice pci;
    IOPlatformExpert platform;
    Navi48Native driver;
    IODeviceMemory *descriptors[3]{};
    Fixture() {
        fake::bootPresent = true; fake::bootValue = 1; fake::offsetPresent = true; fake::bytesPresent = true;
        fake::scratchOffset = 64 * MiB; fake::scratchBytes = 24 * MiB;
        fake::allocationBudget = -1; fake::baseStart = true; fake::lockFails = false; fake::events.clear();
        driver.provider = &pci; fake::platform = &platform;
        pci.set16(0x00, 0x1002); pci.set16(0x02, 0x7550); pci.set16(0x2c, 0x1849); pci.set16(0x2e, 0x5417);
        pci.set32(0x08, 0x030000c0); pci.set16(0x04, 7);
        constexpr uint64_t base[3] = {0x440000000ULL, 0x450000000ULL, 0xfcb00000ULL};
        constexpr uint64_t size[3] = {256 * MiB, 2 * MiB, 512 * 1024};
        constexpr unsigned reg[3] = {0x10, 0x18, 0x24};
        constexpr IOOptionBits options = kIOMapAnywhere | kIOMapUnique | kIOMapReadOnly | kIOMapInhibitCache;
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
        driver.free();
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
        auto *bar = dynamic_cast<OSDictionary *>(report ? report->getObject("BAR0") : nullptr);
        CHECK(value(bar, "CPUPhysical") == 0x440000000ULL);
        IOUserClient *client = reinterpret_cast<IOUserClient *>(uintptr_t{1});
        CHECK(f.driver.newUserClient(nullptr, nullptr, 0, &client) == kIOReturnUnsupported && !client);
        client = reinterpret_cast<IOUserClient *>(uintptr_t{1});
        CHECK(f.driver.newUserClient(nullptr, nullptr, 0, nullptr, &client) == kIOReturnUnsupported && !client);
        callbackDriver = &f.driver; callbackProvider = &f.pci;
        f.pci.afterClose = [](IOPCIDevice *) { callbackDriver->stop(callbackProvider); };
        f.driver.stop(&f.pci); f.closed(); CHECK(f.driver.stops == 1);
        CHECK(!f.driver.start(&f.pci)); // no hot restart/rearm
    }
    // Exact PCI target and partial acquisition propagate failure through start.
    for (bool wrongCard : {false, true}) {
        Fixture f; f.init();
        if (wrongCard) f.pci.set16(0x02, 0x7551);
        else f.descriptors[1]->recipe.fail = true;
        CHECK(!f.driver.start(&f.pci)); f.closed(); CHECK(f.driver.starts == 1 && f.driver.stops == 1);
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
    CHECK(fake::liveObjects == 0 && fake::liveLocks == 0 && fake::mapsMade == fake::mapsFreed);
    std::printf("native_kext_smoke: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
