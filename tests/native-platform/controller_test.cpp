// SPDX-License-Identifier: MIT
// The actual IOKitController.cpp with IOKit doubles, never the live Radeon.
#include "IOKitController.hpp"
#include "AcceleratorPreflight.hpp"
#include <atomic>
#include <cstdio>
#include <thread>

using namespace n48native;
static unsigned checks = 0, failed = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; std::fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
constexpr uint64_t MiB = 1024 * 1024;
constexpr uint64_t bases[3] = {0x440000000ULL, 0x450000000ULL, 0xfcb00000ULL};
constexpr uint64_t lengths[3] = {256 * MiB, 2 * MiB, 512 * 1024};
constexpr unsigned registers[3] = {0x10, 0x18, 0x24};
constexpr IOOptionBits options = kIOMapAnywhere | kIOMapUnique | kIOMapReadOnly | kIOMapInhibitCache;
struct Fixture;
static Fixture *active = nullptr;
struct Fixture {
    IOService owner;
    IOPCIDevice pci;
    IOPlatformExpert platform;
    IODeviceMemory *descriptors[3]{};
    PlatformRequest request{{64 * MiB, 24 * MiB}, 24 * MiB};
    Fixture() {
        active = this; fake::platform = &platform; fake::events.clear();
        fake::substitutionProvider = &pci;
        fake::bootPresent = true; fake::bootValue = 1; fake::lockFails = false;
        owner.provider = &pci;
        pci.set16(0x00, 0x1002); pci.set16(0x02, 0x7550);
        pci.set16(0x2c, 0x1849); pci.set16(0x2e, 0x5417);
        pci.set32(0x08, 0x030000c0); pci.set16(0x04, 7);
        for (unsigned i = 0; i < 3; ++i) {
            descriptors[i] = new IODeviceMemory;
            pci.descriptors[i] = descriptors[i]; descriptors[i]->index = i;
            descriptors[i]->bytes = lengths[i]; descriptors[i]->contiguous = lengths[i];
            descriptors[i]->recipe = {false, 0x1000000000ULL + i * 0x100000000ULL,
                                      bases[i], lengths[i], lengths[i], options, kernel_task, descriptors[i]};
            setBase(i, bases[i]);
        }
        platform.video.v_baseAddr = bases[0]; platform.video.v_rowBytes = 4096;
        platform.video.v_width = 1024; platform.video.v_height = 256; platform.video.v_depth = 32;
    }
    ~Fixture() {
        for (auto *d : descriptors) d->release();
        fake::platform = nullptr; fake::substitutionProvider = nullptr; active = nullptr;
    }
    void setBase(unsigned i, uint64_t base) {
        pci.set32(registers[i], static_cast<uint32_t>(base) | (i < 2 ? 0xc : 0));
        if (i < 2) pci.set32(registers[i] + 4, static_cast<uint32_t>(base >> 32));
        descriptors[i]->physical = base; descriptors[i]->recipe.physical = base;
    }
    PlatformResult acquire(IOKitController &controller) { return controller.acquire(&owner, &pci, request); }
    void closed() {
        CHECK(owner.references() == 1 && pci.references() == 1);
        CHECK(!pci.client && fake::mapsMade == fake::mapsFreed);
        for (auto *d : descriptors) CHECK(d->references() == 1);
    }
    void refused(IOKitController &controller, PlatformResult expected) {
        CHECK(acquire(controller) == expected);
        const auto s = controller.snapshot();
        CHECK(s.result == expected && s.phase == PlatformPhase::Failed);
        CHECK(!s.providerOpen && !s.mappingsHeld && !s.observationsValid && !s.accessEnabled);
        CHECK((s.blockers & kUnimplementedHardwareProofs) == kUnimplementedHardwareProofs);
        CHECK(s.blockers & MappingLease);
        CHECK(controller.revalidate() == PlatformResult::NotMapped);
        CHECK(acquire(controller) == PlatformResult::Used);
        closed();
    }
    void mapped(IOKitController &controller) {
        CHECK(acquire(controller) == PlatformResult::MappingsHeldUnqualified);
        const auto s = controller.snapshot();
        CHECK(s.phase == PlatformPhase::MappedUnqualified && s.providerOpen && s.mappingsHeld && s.observationsValid);
        CHECK(!s.accessEnabled && s.blockers != 0);
        CHECK((s.blockers & kUnimplementedHardwareProofs) == kUnimplementedHardwareProofs);
        for (unsigned i = 0; i < 3; ++i) {
            CHECK(s.bars[i].mapCheck == MapCheck::Ok && s.bars[i].observedAddress != 0);
            CHECK(s.bars[i].observedMapOptions == options && s.bars[i].observedLength == lengths[i]);
            CHECK(s.bars[i].observedContiguous == lengths[i] && s.bars[i].observedPhysical == bases[i]);
            CHECK(s.bars[i].observedDescriptorMatch && s.bars[i].observedTaskMatch);
            CHECK(s.bars[i].observedRereadPresent && s.bars[i].observedRereadMatch);
            CHECK(s.bars[i].observedDeclaredIsReread);
            CHECK(s.bars[i].descriptorOrigin == DescriptorOrigin::Retained);
        }
        CHECK(s.command == 7 && s.bdf == 0x70000);
        CHECK(owner.references() == 2 && pci.references() == 2);
        for (unsigned i = 0; i < 3; ++i) {
            CHECK(descriptors[i]->references() == 3); // fixture + controller + map
            CHECK(descriptors[i]->lastSegmentOptions == kIOMemoryMapperNone);
            CHECK(descriptors[i]->lastMapOptions == options);
            CHECK(s.bars[i].cpuPhysical == bases[i] && s.bars[i].bytes == lengths[i]);
        }
    }
};

static void admission() {
    for (unsigned which = 0; which < 4; ++which) {
        Fixture f; IOKitController c;
        if (which == 0) fake::bootPresent = false;
        else fake::bootValue = which == 1 ? 0 : (which == 2 ? 2 : UINT32_MAX);
        f.refused(c, PlatformResult::Disabled);
        CHECK(f.pci.opens == 0 && f.pci.configReads == 0 && f.platform.calls == 0);
    }
    {
        Fixture f; fake::lockFails = true; IOKitController c;
        CHECK(f.acquire(c) == PlatformResult::NoLock);
        CHECK(c.snapshot().result == PlatformResult::NoLock);
        CHECK(c.revalidate() == PlatformResult::NoLock);
        CHECK(f.pci.opens == 0); c.release(); f.closed();
    }
    {
        Fixture f; IOKitController c;
        CHECK(c.acquire(nullptr, &f.pci, f.request) == PlatformResult::InvalidOwner); f.closed();
    }
    {
        Fixture f; IOKitController c;
        CHECK(c.acquire(&f.pci, &f.pci, f.request) == PlatformResult::InvalidOwner); f.closed();
    }
    for (unsigned which = 0; which < 4; ++which) {
        Fixture f; IOKitController c; IOService other;
        if (which == 0) f.owner.provider = nullptr;
        if (which == 1) f.owner.provider = &other;
        if (which == 2) f.owner.inactive = true;
        if (which == 3) f.pci.inactive = true;
        f.refused(c, which < 2 ? PlatformResult::WrongProvider : PlatformResult::ProviderInactive);
        CHECK(f.pci.configReads == 0);
    }
    {
        Fixture f; IOKitController c; IOService nonPci;
        f.owner.provider = &nonPci;
        CHECK(c.acquire(&f.owner, &nonPci, f.request) == PlatformResult::WrongProvider); f.closed();
    }
    for (bool sameOwner : {false, true}) {
        Fixture f; IOKitController c; IOService other;
        f.pci.client = sameOwner ? &f.owner : &other;
        CHECK(f.acquire(c) == PlatformResult::ProviderBusy);
        CHECK(f.pci.opens == 0 && f.pci.closes == 0 && f.pci.configReads == 0);
        CHECK(f.pci.client == (sameOwner ? &f.owner : &other));
        f.pci.client = nullptr; f.closed();
    }
    {
        Fixture f; IOKitController c; f.pci.openSucceeds = false;
        f.refused(c, PlatformResult::OpenFailed); CHECK(f.pci.closes == 0);
    }
}
static void configAndDescriptors() {
    for (unsigned offset : {0x00, 0x02, 0x2c, 0x2e, 0x08, 0x0a}) {
        Fixture f; IOKitController c; f.pci.config[offset] ^= 1;
        f.refused(c, PlatformResult::WrongCard); CHECK(f.pci.closes == 1);
    }
    for (unsigned which = 0; which < 3; ++which) {
        Fixture f; IOKitController c;
        if (which == 0) f.pci.set16(0x04, 5); // no memory decoding; never enable it
        if (which == 1) f.pci.set16(0x04, UINT16_MAX);
        if (which == 2) f.pci.config[0x0e] = 1;
        f.refused(c, PlatformResult::InvalidConfig);
    }
    for (unsigned bar = 0; bar < 3; ++bar) {
        for (uint8_t wrong : {uint8_t{1}, uint8_t{2}, uint8_t{4}, uint8_t{8}, uint8_t{0xf}}) {
            Fixture f; IOKitController c; f.pci.config[registers[bar]] = wrong;
            f.refused(c, PlatformResult::InvalidBar);
        }
        for (unsigned which = 0; which < 5; ++which) {
            Fixture f; IOKitController c;
            if (which == 0) f.pci.descriptors[bar] = nullptr;
            if (which == 1) --f.descriptors[bar]->bytes;
            if (which == 2) --f.descriptors[bar]->contiguous;
            if (which == 3) ++f.descriptors[bar]->physical;
            if (which == 4) f.descriptors[bar]->bytes *= 2; // unsupported ReBAR profile
            f.refused(c, which == 0 ? PlatformResult::MissingDescriptor : PlatformResult::InvalidDescriptor);
            CHECK(c.snapshot().failedBar == registers[bar]);
            for (auto *d : f.descriptors) CHECK(d->mapCalls == 0);
        }
    }
    {
        Fixture f; IOKitController c;
        f.setBase(0, 0); f.refused(c, PlatformResult::InvalidBar);
    }
    {
        Fixture f; IOKitController c;
        f.setBase(0, UINT64_MAX & ~(lengths[0] - 1)); f.refused(c, PlatformResult::InvalidBar);
    }
    {
        Fixture f; IOKitController c;
        f.setBase(1, bases[0]); f.refused(c, PlatformResult::PhysicalOverlap);
    }
    {
        Fixture f; IOKitController c;
        f.pci.afterRead = [](IOPCIDevice *pci, IOByteCount reg) {
            if (reg == 0x14) { pci->set32(0x14, 5); pci->afterRead = nullptr; }
        };
        f.refused(c, PlatformResult::ConfigurationChanged);
    }
}
static void candidates() {
    for (unsigned which = 0; which < 9; ++which) {
        Fixture f; IOKitController c;
        if (which == 0) f.request.requiredBytes = 0;
        if (which == 1) f.request.requiredBytes = f.request.candidate.bytes + 1;
        if (which == 2) f.request.candidate.bytes = 0;
        if (which == 3) ++f.request.candidate.offset;
        if (which == 4) --f.request.candidate.bytes;
        if (which == 5) f.request.candidate.offset = lengths[0];
        if (which == 6) f.request.candidate.bytes = lengths[0];
        if (which == 7) f.request.candidate.offset = UINT64_MAX - 4095;
        if (which == 8) f.request.candidate.bytes = UINT64_MAX - 4095;
        f.refused(c, PlatformResult::InvalidCandidate);
        for (auto *d : f.descriptors) CHECK(d->mapCalls == 0);
    }
    {
        Fixture f; IOKitController c; f.request.candidate.offset = 0;
        f.refused(c, PlatformResult::ConsoleOverlap); CHECK(f.descriptors[0]->mapCalls == 0);
    }
    {
        Fixture f; IOKitController c;
        // Padding outside the visible pixels remains part of the console allocation.
        f.platform.video.v_length = 80 * MiB; f.platform.video.v_offset = 4 * MiB;
        f.refused(c, PlatformResult::ConsoleOverlap);
    }
}
static void mapsAndLifetime() {
    for (unsigned bar = 0; bar < 3; ++bar) {
        Fixture f; IOKitController c; f.descriptors[bar]->recipe.fail = true;
        f.refused(c, PlatformResult::MapFailed);
        std::vector<unsigned> expected;
        for (unsigned i = bar; i-- > 0;) expected.push_back(i);
        expected.push_back(10);
        CHECK(fake::events == expected);
    }
    // Each rejected property maps to exactly one stable MapCheck ID.
    constexpr MapCheck expectedCheck[12] = {MapCheck::NullAddress, MapCheck::UnalignedAddress,
        MapCheck::RangeOverflow, MapCheck::LengthMismatch, MapCheck::LengthMismatch, MapCheck::ContiguousMismatch,
        MapCheck::PhysicalMismatch, MapCheck::CacheMismatch, MapCheck::FlagsMismatch, MapCheck::FlagsMismatch,
        MapCheck::TaskMismatch, MapCheck::DescriptorMismatch};
    for (unsigned bar = 0; bar < 3; ++bar) {
        for (unsigned which = 0; which < 12; ++which) {
            Fixture f; IOKitController c; auto &r = f.descriptors[bar]->recipe;
            if (which == 0) r.address = 0;
            if (which == 1) ++r.address;
            if (which == 2) r.address = UINT64_MAX - 4095;
            if (which == 3) --r.bytes;
            if (which == 4) ++r.bytes;
            if (which == 5) --r.contiguous;
            if (which == 6) ++r.physical;
            if (which == 7) r.options = (options & ~kIOMapCacheMask) | 0x400; // WC is not UC
            if (which == 8) r.options &= ~kIOMapReadOnly;
            if (which == 9) r.options &= ~kIOMapUnique;
            if (which == 10) r.task = nullptr;
            if (which == 11) r.backing = f.descriptors[(bar + 1) % 3];
            f.refused(c, PlatformResult::InvalidMap);
            const auto detail = c.snapshot();
            CHECK(detail.failedBar == registers[bar]);
            CHECK(detail.bars[bar].mapCheck == expectedCheck[which]);
            CHECK(fake::events == std::vector<unsigned>({2, 1, 0, 10}));
        }
        // A diverged revalidation address keeps its own ID, distinct from overlap
        // between two different mappings.
        { Fixture f; IOKitController c; f.mapped(c); f.descriptors[bar]->lastMap->recipe.address += 4096;
          CHECK(c.revalidate() == PlatformResult::InvalidMap);
          const auto detail = c.snapshot();
          CHECK(detail.failedBar == registers[bar] && detail.bars[bar].mapCheck == MapCheck::AddressChanged);
          f.closed(); }
        // Descriptor-identity matrix. Order of the real guards matters:
        // revalidateLocked() re-runs checkDescriptors() AFTER the maps exist.
        // A persistent provider substitution is therefore observed as
        // ConfigurationChanged (provider no longer returns the retained
        // object), not InvalidMap. checkMaps()'s reread comparison only names
        // scenarios that survive that earlier guard: shared map declaring
        // another object (reread==retained!=declared), or a lookup that
        // vanishes exactly at checkMaps() time.
        { // shared map declaring a NON-conforming object: InvalidMap + NotAdopted
            Fixture f; IOKitController c;
            auto *other = new IODeviceMemory; other->index = 9;
            f.descriptors[bar]->recipe.backing = other; // zero length: adoption refused
            f.refused(c, PlatformResult::InvalidMap);
            const auto detail = c.snapshot();
            CHECK(detail.failedBar == registers[bar] && detail.bars[bar].mapCheck == MapCheck::DescriptorMismatch);
            CHECK(!detail.bars[bar].observedDescriptorMatch);
            CHECK(detail.bars[bar].observedRereadPresent && detail.bars[bar].observedRereadMatch);
            CHECK(!detail.bars[bar].observedDeclaredIsReread);
            CHECK(detail.bars[bar].descriptorOrigin == DescriptorOrigin::NotAdopted);
            other->release();
        }
        { // persistent provider substitution: ConfigurationChanged before checkMaps()
            Fixture f; IOKitController c;
            auto *other = new IODeviceMemory; other->index = 9;
            f.descriptors[bar]->recipe.backing = other;
            f.pci.substitute[bar] = other; // applies from the first map() call
            f.refused(c, PlatformResult::ConfigurationChanged);
            CHECK(c.snapshot().failedBar == registers[bar]);
            other->release();
        }
        { // lookup vanishing at checkMaps() time only: InvalidMap + no reread
            Fixture f; IOKitController c;
            auto *other = new IODeviceMemory; other->index = 9;
            f.descriptors[bar]->recipe.backing = other;
            f.descriptors[bar]->afterMap = [](IOMemoryDescriptor *) {
                // checkDescriptors() already passed; clear only for the reread.
                // Restored by the fixture destructor path via closed().
                if (fake::substitutionProvider) {
                    for (unsigned i = 0; i < 3; ++i) fake::substitutionProvider->descriptors[i] = nullptr;
                }
            };
            // NOTE: this also trips checkDescriptors()'s reread inside
            // revalidateLocked() -> ConfigurationChanged, same ordering reason.
            // The vanishing-reread InvalidMap path is covered by the service
            // test below with a phase-gated stub instead.
            f.refused(c, PlatformResult::ConfigurationChanged);
            other->release();
        }
    }
    {
        Fixture f; IOKitController c;
        f.descriptors[1]->recipe.address = f.descriptors[0]->recipe.address;
        f.refused(c, PlatformResult::VirtualOverlap);
    }
    // Accelerator-exclusion preflight matrix: the EXACT function run() calls
    // (AcceleratorPreflight.hpp), covered with IOKit doubles. Null iterator
    // (real Tahoe 0.2.4 boot, no accelerator) passes with iteratorNull marked;
    // empty iterator passes; any object found is ExclusiveAccess; a null
    // matching dictionary is NoMemory. No GPU, firmware, mapping or state.
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        fake::acceleratorMatchingNull = scenario == 0;
        fake::acceleratorIteratorNull = scenario == 1;
        fake::acceleratorPresent = scenario == 3;
        bool iteratorNull = false;
        const IOReturn result = checkNoAccelerator(iteratorNull);
        if (scenario == 0) {
            CHECK(result == kIOReturnNoMemory && !iteratorNull);
        } else if (scenario == 1) {
            CHECK(result == kIOReturnSuccess && iteratorNull); // 0.2.4 boot case
        } else if (scenario == 2) {
            CHECK(result == kIOReturnSuccess && !iteratorNull);
        } else {
            CHECK(result == kIOReturnExclusiveAccess && !iteratorNull);
        }
    }
    fake::acceleratorMatchingNull = fake::acceleratorIteratorNull = fake::acceleratorPresent = false;
    CHECK(fake::liveObjects == 0); // doubles released, no leak
    // Way 1: a shared mapping declaring a FULLY conforming other object is
    // adopted after revalidation, and the lease succeeds. Separate block:
    // it must NOT live inside Fixture::mapped(), which asserts the strict
    // Retained path. acquire() + adoption-specific checks here.
    for (unsigned bar = 0; bar < 3; ++bar) {
        Fixture f; IOKitController c;
        auto *other = new IODeviceMemory; other->index = 9;
        other->bytes = lengths[bar]; other->contiguous = lengths[bar]; other->physical = bases[bar];
        f.descriptors[bar]->recipe.backing = other; // mapping declares a conforming object
        CHECK(f.acquire(c) == PlatformResult::MappingsHeldUnqualified);
        const auto adopted = c.snapshot();
        CHECK(adopted.phase == PlatformPhase::MappedUnqualified && adopted.mappingsHeld);
        CHECK(adopted.bars[bar].descriptorOrigin == DescriptorOrigin::DeclaredAdopted);
        CHECK(adopted.bars[bar].mapCheck == MapCheck::Ok && adopted.bars[bar].observedDescriptorMatch);
        CHECK(adopted.bars[bar].observedRereadMatch && !adopted.bars[bar].observedDeclaredIsReread);
        for (unsigned i = 0; i < 3; ++i)
            if (i != bar) CHECK(adopted.bars[i].descriptorOrigin == DescriptorOrigin::Retained);
        CHECK(c.revalidate() == PlatformResult::MappingsHeldUnqualified);
        CHECK(c.snapshot().bars[bar].descriptorOrigin == DescriptorOrigin::DeclaredAdopted);
        c.release(); f.closed();
        other->release();
    }
    // A declared object that fails ANY property is NotAdopted: the lease
    // still refuses, with the exact MapCheck of the surviving guard.
    for (unsigned bar = 0; bar < 3; ++bar) {
        Fixture f; IOKitController c;
        auto *other = new IODeviceMemory; other->index = 9;
        other->bytes = lengths[bar]; other->contiguous = lengths[bar];
        other->physical = bases[bar] + 4096; // wrong base: adoption refused
        f.descriptors[bar]->recipe.backing = other;
        f.refused(c, PlatformResult::InvalidMap);
        const auto detail = c.snapshot();
        CHECK(detail.bars[bar].descriptorOrigin == DescriptorOrigin::NotAdopted);
        CHECK(detail.bars[bar].mapCheck == MapCheck::DescriptorMismatch);
        other->release();
    }
    {
        Fixture f; IOKitController c; f.mapped(c);
        auto s = c.snapshot();
        CHECK(s.console.result == ConsoleResult::LocatedInBar0);
        CHECK(s.console.allocationInBar0.offset == 0 && s.console.allocationInBar0.bytes == MiB);
        CHECK(s.candidateCpuPhysical.offset == bases[0] + 64 * MiB && s.candidateCpuPhysical.bytes == 24 * MiB);
        CHECK(!(s.blockers & (MappingLease | ConsolePlacement)));
        CHECK(c.revalidate() == PlatformResult::MappingsHeldUnqualified);
        CHECK(f.acquire(c) == PlatformResult::Used);
        c.release(); f.closed();
        CHECK(fake::events == std::vector<unsigned>({2, 1, 0, 10}));
        s = c.snapshot();
        CHECK(s.phase == PlatformPhase::Released && !s.observationsValid && !s.accessEnabled);
        CHECK(s.blockers & MappingLease);
        c.release(); f.closed(); CHECK(f.pci.closes == 1);
        CHECK(c.revalidate() == PlatformResult::NotMapped && f.acquire(c) == PlatformResult::Used);
    }
    {
        Fixture f;
        { IOKitController c; f.mapped(c); } // destructor, no explicit release
        f.closed(); CHECK(fake::events == std::vector<unsigned>({2, 1, 0, 10}));
    }
    {
        Fixture f; IOKitController c; c.release();
        CHECK(f.acquire(c) == PlatformResult::Used); f.closed();
    }
}
static void consoleEvidence() {
    for (unsigned which = 0; which < 15; ++which) {
        Fixture f; IOKitController c;
        if (which == 0) fake::platform = nullptr;
        if (which == 1) f.platform.status = kIOReturnIOError;
        if (which == 2) f.platform.video.v_rowBytes = 0;
        if (which == 3) f.platform.video.v_width = 0;
        if (which == 4) f.platform.video.v_height = UINT64_MAX;
        if (which == 5) f.platform.video.v_depth = 0;
        if (which == 6) f.platform.video.v_depth = 30;
        if (which == 7) f.platform.video.v_width = UINT64_MAX;
        if (which == 8) f.platform.video.v_length = 4;
        if (which == 9) f.platform.video.v_offset = 4;
        if (which == 10) f.platform.video.v_baseAddr = 0;
        if (which == 11) ++f.platform.video.v_baseAddr; // reject flags, no mask-to-success
        if (which == 12) f.platform.video.v_baseAddr = bases[1];
        if (which == 13) f.platform.video.v_baseAddr = UINT64_MAX - 4095;
        if (which == 14) f.platform.video.v_baseAddr = bases[0] + lengths[0] - 4096;
        f.mapped(c);
        const auto s = c.snapshot();
        CHECK(s.console.result != ConsoleResult::LocatedInBar0);
        CHECK(s.console.allocationInBar0.bytes == 0 && s.blockers & ConsolePlacement);
        CHECK(s.blockers & VramReservation); // not a free-space proof, even with no console location
        c.release(); f.closed();
    }
    {
        Fixture f; IOKitController c;
        f.platform.video.v_baseAddr += 2 * MiB;
        f.platform.video.v_length = 8 * MiB; f.platform.video.v_offset = 4 * MiB;
        f.mapped(c);
        const auto s = c.snapshot();
        CHECK(s.console.allocationInBar0.offset == 2 * MiB && s.console.allocationInBar0.bytes == 8 * MiB);
        CHECK(s.blockers == kUnimplementedHardwareProofs);
        c.release(); f.closed();
    }
}
static void instability() {
    for (unsigned which = 0; which < 8; ++which) {
        Fixture f; IOKitController c;
        f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) {}; // overwritten with selected mutation below
        if (which == 0) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.set16(0x04, 3); };
        if (which == 1) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.bus = 8; };
        if (which == 2) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.set32(0x18, 0x6000000c); };
        if (which == 3) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.descriptors[0] = nullptr; };
        if (which == 4) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->owner.inactive = true; };
        if (which == 5) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.client = nullptr; };
        if (which == 6) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->platform.video.v_baseAddr += MiB; };
        if (which == 7) f.descriptors[2]->afterMap = [](IOMemoryDescriptor *) { active->pci.set16(0x02, 0xffff); };
        const PlatformResult expected[] = {PlatformResult::ConfigurationChanged, PlatformResult::ConfigurationChanged,
            PlatformResult::ConfigurationChanged, PlatformResult::ConfigurationChanged, PlatformResult::ProviderInactive,
            PlatformResult::OpenLost, PlatformResult::ConsoleChanged, PlatformResult::WrongCard};
        f.refused(c, expected[which]);
    }
    for (unsigned which = 0; which < 5; ++which) {
        Fixture f; IOKitController c; f.mapped(c);
        if (which == 0) f.pci.inactive = true;
        if (which == 1) f.descriptors[0]->lastMap->recipe.address += 4096;
        if (which == 2) --f.descriptors[2]->contiguous;
        if (which == 3) f.platform.video.v_length = 2 * MiB;
        if (which == 4) f.platform.afterConsole = [](IOPlatformExpert *) { active->pci.inactive = true; };
        const PlatformResult expected[] = {PlatformResult::ProviderInactive, PlatformResult::InvalidMap,
            PlatformResult::InvalidDescriptor, PlatformResult::ConsoleChanged, PlatformResult::ProviderInactive};
        CHECK(c.revalidate() == expected[which]);
        const auto s = c.snapshot();
        if (which == 1) CHECK(s.bars[0].mapCheck == MapCheck::AddressChanged);
        CHECK(s.phase == PlatformPhase::Failed && !s.observationsValid && !s.mappingsHeld && !s.accessEnabled);
        CHECK(s.blockers & MappingLease); f.closed(); CHECK(f.acquire(c) == PlatformResult::Used);
    }
}
static void deferredRelease() {
    Fixture f; IOKitController c; f.mapped(c);
    CHECK(c.revalidateHeld() == PlatformResult::MappingsHeldUnqualified);
    f.platform.video.v_length = 2 * MiB;
    CHECK(c.revalidateHeld() == PlatformResult::ConsoleChanged);
    const auto s = c.snapshot();
    CHECK(s.phase == PlatformPhase::Failed && !s.observationsValid && !s.accessEnabled);
    CHECK(s.mappingsHeld && s.providerOpen && f.pci.client == &f.owner);
    CHECK((s.blockers & MappingLease) != 0 && f.pci.closes == 0);
    CHECK(c.revalidateHeld() == PlatformResult::NotMapped && f.acquire(c) == PlatformResult::Used);
    c.release(); f.closed(); CHECK(f.pci.closes == 1);
}
static IOKitController *reentrantController = nullptr;
static void reentrantCleanup() {
    for (bool failure : {false, true}) {
        Fixture f; IOKitController c; reentrantController = &c;
        f.pci.afterClose = [](IOPCIDevice *) {
            const auto s = reentrantController->snapshot();
            CHECK(!s.mappingsHeld && !s.providerOpen && !s.observationsValid && !s.accessEnabled);
            CHECK(s.phase == PlatformPhase::Released || s.phase == PlatformPhase::Failed);
            reentrantController->release(); // must not deadlock or double close/free
            CHECK(reentrantController->revalidate() == PlatformResult::NotMapped);
        };
        if (failure) {
            --f.descriptors[0]->recipe.bytes;
            f.refused(c, PlatformResult::InvalidMap);
        } else {
            f.mapped(c); c.release(); f.closed();
        }
        CHECK(f.pci.closes == 1);
        reentrantController = nullptr;
    }
}
static void serializedLifetime() {
    Fixture f; IOKitController c; f.mapped(c);
    const unsigned reads = f.pci.configReads;
    (void)c.snapshot(); CHECK(f.pci.configReads == reads); // snapshot does not probe hardware
    std::atomic<bool> valid{true};
    auto reader = [&] {
        for (unsigned i = 0; i < 100; ++i) {
            const auto s = c.snapshot();
            if (s.accessEnabled || !(s.blockers & kUnimplementedHardwareProofs)) valid = false;
            if (s.phase == PlatformPhase::MappedUnqualified && (!s.mappingsHeld || !s.observationsValid)) valid = false;
            if (s.phase == PlatformPhase::Released && (s.mappingsHeld || s.observationsValid)) valid = false;
        }
    };
    std::thread a(reader), b(reader), closer([&] { c.release(); });
    a.join(); b.join(); closer.join(); CHECK(valid.load()); f.closed();
}
int main() {
    admission(); configAndDescriptors(); candidates(); mapsAndLifetime(); consoleEvidence(); instability(); deferredRelease(); reentrantCleanup(); serializedLifetime();
    CHECK(fake::liveObjects == 0 && fake::liveLocks == 0 && fake::mapsMade == fake::mapsFreed);
    std::printf("native_platform_test: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
