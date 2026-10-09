#include "Navi48PciProbe.hpp"
#include "ProbePolicy.hpp"
#include <FakeIOKit.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned checks = 0, failed = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failed; std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); } } while (0)
using Bytes = std::vector<uint8_t>;
constexpr const char *snapshotKey = "Navi48PCI,RegistrySnapshot";

void put32(Bytes &bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = uint8_t(value >> (8 * i));
}
Bytes encoded(uint32_t value) { Bytes result(4); put32(result, 0, value); return result; }
Bytes addresses() {
    // Sanitized IORegistry observation on 25G241; no hardware access by this test.
    const uint32_t words[] = {
        0xc2070010, 4, 0x40000000, 0, 0x10000000,
        0xc2070018, 4, 0x50000000, 0, 0x00200000,
        0x81070020, 0, 0x0000e000, 0, 0x00000100,
        0x82070024, 0, 0xfcb00000, 0, 0x00080000,
        0x82070030, 0, 0xfcb80000, 0, 0x00020000
    };
    Bytes result(sizeof(words));
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i) put32(result, i * 4, words[i]);
    return result;
}
void setBytes(IOService &provider, const char *key, const Bytes &bytes) {
    OSData *data = OSData::withBytes(bytes.data(), static_cast<unsigned>(bytes.size()));
    if (!data || !provider.setProperty(key, data)) std::abort();
    data->release();
}
void fixture(IOService &provider) {
    for (unsigned i = 0; i < n48pci::kIdentityCount; ++i)
        setBytes(provider, n48pci::kIdentityKeys[i], encoded(n48pci::kTarget[i]));
    setBytes(provider, "assigned-addresses", addresses());
}
void enabled() { fake::bootPresent = true; fake::bootValue = 1; }
void denied(IOService &provider) {
    const unsigned before = provider.writes;
    const int live = fake::live;
    {
        Navi48PciProbe module;
        SInt32 score = 0;
        CHECK(module.probe(&provider, &score) == nullptr);
        CHECK(!module.start(&provider));
        CHECK(module.propertyCount() == 0);
        CHECK(module.probes == 0 && module.starts == 0 && module.stops == 0);
        CHECK(provider.writes == before);
    }
    CHECK(fake::live == live);
}
uint64_t number(OSDictionary *dict, const char *key) {
    OSNumber *value = dict ? OSDynamicCast(OSNumber, dict->getObject(key)) : nullptr;
    CHECK(value != nullptr);
    return value ? value->unsigned64BitValue() : 0;
}
void accepted(IOPCIDevice &provider) {
    const unsigned before = provider.writes;
    const int live = fake::live;
    const unsigned logs = fake::logs;
    {
        Navi48PciProbe module;
        SInt32 score = 0;
        CHECK(module.probe(&provider, &score) == &module);
        CHECK(module.start(&provider));
        CHECK(module.propertyCount() == 1);
        auto *report = OSDynamicCast(OSDictionary, module.getProperty(snapshotKey));
        CHECK(report != nullptr);
        CHECK(number(report, "SchemaVersion") == 1);
        CHECK(number(report, "ResourceCount") == 5);
        CHECK(number(report, "PCIBDF") == 0x70000);
        for (unsigned i = 0; i < n48pci::kIdentityCount; ++i)
            CHECK(number(report, n48pci::kIdentityKeys[i]) == n48pci::kTarget[i]);
        auto *bar0 = report ? OSDynamicCast(OSDictionary, report->getObject("BAR0")) : nullptr;
        CHECK(number(bar0, "Base") == 0x440000000ull);
        CHECK(number(bar0, "Length") == 256ull * 1024 * 1024);
        CHECK(provider.writes == before);
        CHECK(module.probes == 1 && module.starts == 1 && module.stops == 0);
        module.stop(&provider);
        CHECK(module.propertyCount() == 0 && module.stops == 1);
        CHECK(provider.writes == before);
    }
    CHECK(fake::live == live);
    CHECK(fake::logs == logs + 1);
}
void policyTests() {
    CHECK(!n48pci::enabled(false, 1));
    CHECK(!n48pci::enabled(true, 0));
    CHECK(!n48pci::enabled(true, 2));
    CHECK(!n48pci::enabled(true, UINT32_MAX));
    CHECK(n48pci::enabled(true, 1));
    CHECK(!n48pci::target(nullptr));
    uint32_t value = 99;
    CHECK(!n48pci::word(nullptr, 4, value) && value == 99);
    const auto le = encoded(0x12345678);
    CHECK(n48pci::word(le.data(), le.size(), value) && value == 0x12345678);
    for (unsigned length : {0u, 1u, 2u, 3u, 5u, 8u})
        CHECK(!n48pci::word(le.data(), length, value));

    const auto valid = addresses();
    n48pci::Snapshot result{};
    CHECK(n48pci::resources(valid.data(), valid.size(), result) == n48pci::ParseResult::Ok);
    CHECK(result.count == 5 && result.resources[0].base == 0x440000000ull);
    auto refuse = [&](Bytes input, n48pci::ParseResult expected) {
        n48pci::Snapshot sentinel = result;
        CHECK(n48pci::resources(input.data(), input.size(), sentinel) == expected);
        CHECK(std::memcmp(&sentinel, &result, sizeof(result)) == 0);
    };
    CHECK(n48pci::resources(nullptr, valid.size(), result) == n48pci::ParseResult::BadLength);
    for (unsigned length = 0; length <= 141; ++length) {
        if (length && length <= 140 && length % 20 == 0) continue;
        refuse(Bytes(length), n48pci::ParseResult::BadLength);
    }
    for (auto mutation : {
        std::pair<unsigned, uint32_t>{0, 0x80070010}, // config space
        {0, 0x86070010}, // reserved flag
    }) {
        auto bytes = valid; put32(bytes, mutation.first, mutation.second);
        refuse(bytes, n48pci::ParseResult::BadFlags);
    }
    for (uint32_t reg : {0u, 0x11u, 0x28u, 0x34u, 0xffu}) {
        auto bytes = valid; put32(bytes, 0, 0xc2070000u | reg);
        refuse(bytes, n48pci::ParseResult::BadRegister);
    }
    auto bytes = valid; put32(bytes, 20, 0xc2070010);
    refuse(bytes, n48pci::ParseResult::DuplicateRegister);
    bytes = valid; put32(bytes, 20, 0xc2080018);
    refuse(bytes, n48pci::ParseResult::MixedBdf);
    bytes = valid; put32(bytes, 4, 0); put32(bytes, 8, 0);
    refuse(bytes, n48pci::ParseResult::BadRange);
    bytes = valid; put32(bytes, 16, 0);
    refuse(bytes, n48pci::ParseResult::BadRange);
    bytes = valid; put32(bytes, 4, UINT32_MAX); put32(bytes, 8, UINT32_MAX);
    refuse(bytes, n48pci::ParseResult::BadRange);
    bytes = valid; put32(bytes, 28, 0x40000001);
    refuse(bytes, n48pci::ParseResult::OverlappingRanges);
    bytes = valid; put32(bytes, 20, 0x81070018);
    refuse(bytes, n48pci::ParseResult::WrongRequiredSpace);
    for (unsigned index : {0u, 1u, 3u}) {
        bytes = valid; bytes.erase(bytes.begin() + index * 20, bytes.begin() + (index + 1) * 20);
        refuse(bytes, n48pci::ParseResult::MissingRequiredBar);
    }
    bytes = valid; bytes.resize(160);
    refuse(bytes, n48pci::ParseResult::BadLength);
    bytes = valid; std::swap_ranges(bytes.begin(), bytes.begin() + 20, bytes.begin() + 20);
    CHECK(n48pci::resources(bytes.data(), bytes.size(), result) == n48pci::ParseResult::Ok);
    bytes = valid; put32(bytes, 0, 0xc3070010); // memory-64 metadata also accepted
    CHECK(n48pci::resources(bytes.data(), bytes.size(), result) == n48pci::ParseResult::Ok);
    bytes = valid; put32(bytes, 28, 0x50000000); // ranges touching exactly are accepted
    CHECK(n48pci::resources(bytes.data(), bytes.size(), result) == n48pci::ParseResult::Ok);
}
void adapterTests() {
    for (bool present : {false, true}) for (uint32_t flag : {0u, 2u, UINT32_MAX}) {
        IOPCIDevice provider; fixture(provider);
        fake::bootPresent = present; fake::bootValue = flag;
        denied(provider);
        CHECK(provider.reads == 0);
    }
    { IOPCIDevice provider; fixture(provider); fake::bootPresent = false; fake::bootValue = 1;
      denied(provider); CHECK(provider.reads == 0); }
    enabled();
    { IOService notPci; fixture(notPci); denied(notPci); CHECK(notPci.reads == 0); }
    { Navi48PciProbe module; CHECK(module.probe(nullptr, nullptr) == nullptr); CHECK(!module.start(nullptr)); }
    for (unsigned field = 0; field < n48pci::kIdentityCount; ++field) {
        const char *key = n48pci::kIdentityKeys[field];
        { IOPCIDevice p; fixture(p); p.removeProperty(key); denied(p); }
        for (unsigned length : {0u, 1u, 3u, 5u, 8u}) {
            IOPCIDevice p; fixture(p); setBytes(p, key, Bytes(length)); denied(p);
        }
        for (uint32_t value : {n48pci::kTarget[field] ^ 1u, n48pci::kTarget[field] | 0x80000000u}) {
            IOPCIDevice p; fixture(p); setBytes(p, key, encoded(value)); denied(p);
            CHECK(p.reads == 2 * n48pci::kIdentityCount); // never reads assigned-addresses
        }
        { IOPCIDevice p; fixture(p); OSNumber *wrongType = OSNumber::withNumber(n48pci::kTarget[field], 32);
          CHECK(p.setProperty(key, wrongType)); wrongType->release(); denied(p); }
    }
    { IOPCIDevice p; fixture(p); p.removeProperty("assigned-addresses"); denied(p); }
    { IOPCIDevice p; fixture(p); setBytes(p, "assigned-addresses", Bytes(19)); denied(p); }
    { IOPCIDevice p; fixture(p); accepted(p); accepted(p); }
    // start must re-read both the opt-in and the identity after successful probe.
    for (bool alterIdentity : {false, true}) {
        enabled(); IOPCIDevice p; fixture(p); Navi48PciProbe module;
        CHECK(module.probe(&p, nullptr) == &module);
        if (alterIdentity) setBytes(p, "subsystem-id", encoded(0x5418));
        else fake::bootValue = 0;
        CHECK(!module.start(&p)); CHECK(module.propertyCount() == 0); CHECK(module.starts == 0);
    }
    enabled();
    { IOPCIDevice p; fixture(p); Navi48PciProbe module; fake::baseProbeSucceeds = false;
      CHECK(module.probe(&p, nullptr) == nullptr); fake::baseProbeSucceeds = true; }
    { IOPCIDevice p; fixture(p); Navi48PciProbe module; const int live = fake::live;
      fake::baseStartSucceeds = false; CHECK(!module.start(&p)); fake::baseStartSucceeds = true;
      CHECK(module.starts == 1 && module.stops == 0); CHECK(module.propertyCount() == 0);
      CHECK(fake::live == live); }
    // Every allocation/insertion failure up to the first successful start.
    bool succeeded = false;
    unsigned balancedFailure = 0;
    for (int budget = 0; budget < 200 && !succeeded; ++budget) {
        IOPCIDevice p; fixture(p); Navi48PciProbe module;
        const unsigned writes = p.writes;
        const int live = fake::live;
        fake::budget = budget;
        succeeded = module.start(&p);
        fake::budget = -1;
        if (succeeded) module.stop(&p);
        else {
            CHECK(module.propertyCount() == 0);
            if (module.starts) { CHECK(module.stops == 1); ++balancedFailure; }
        }
        CHECK(p.writes == writes); CHECK(fake::live == live);
    }
    CHECK(succeeded && balancedFailure > 0);
    // Both IOServiceOpen overloads reject every client type, even N48N.
    Navi48PciProbe module;
    for (uint32_t type : {0u, 1u, 0x4e34384eu, UINT32_MAX}) {
        IOUserClient *client = reinterpret_cast<IOUserClient *>(uintptr_t(1));
        CHECK(module.newUserClient(nullptr, nullptr, type, &client) == kIOReturnUnsupported);
        CHECK(client == nullptr);
        client = reinterpret_cast<IOUserClient *>(uintptr_t(1));
        CHECK(module.newUserClient(nullptr, nullptr, type, nullptr, &client) == kIOReturnUnsupported);
        CHECK(client == nullptr);
    }
    CHECK(module.newUserClient(nullptr, nullptr, 0, nullptr) == kIOReturnUnsupported);
    CHECK(module.newUserClient(nullptr, nullptr, 0, nullptr, nullptr) == kIOReturnUnsupported);
}
Bytes unhex(const char *text) {
    const size_t n = std::strlen(text);
    if (n % 2 || n > 280) throw std::string("invalid hex length");
    Bytes result;
    for (size_t i = 0; i < n; i += 2) {
        const std::string digits = "0123456789abcdef";
        const size_t a = digits.find(text[i]), b = digits.find(text[i + 1]);
        if (a == std::string::npos || b == std::string::npos) throw std::string("invalid hex");
        result.push_back(uint8_t((a << 4) | b));
    }
    return result;
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 9 && std::strcmp(argv[1], "--registry") == 0) {
        try {
            enabled(); IOPCIDevice p;
            for (unsigned i = 0; i < n48pci::kIdentityCount; ++i)
                setBytes(p, n48pci::kIdentityKeys[i], unhex(argv[i + 2]));
            setBytes(p, "assigned-addresses", unhex(argv[8]));
            Navi48PciProbe module;
            if (!module.start(&p)) return 1;
            module.stop(&p);
            std::puts("Registry metadata admitted by the host-compiled kext; no kernel/GPU execution.");
            return 0;
        } catch (...) { return 2; }
    }
    if (argc != 1) return 2;
    policyTests();
    adapterTests();
    CHECK(fake::live == 0);
    std::printf("pci_probe_test: %u checks, %u failed\n", checks, failed);
    return failed ? 1 : 0;
}
