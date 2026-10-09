#include "Navi48PciProbe.hpp"
#include "ProbePolicy.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

OSDefineMetaClassAndStructors(Navi48PciProbe, IOService)

namespace {
constexpr const char *kSnapshotKey = "Navi48PCI,RegistrySnapshot";

bool readSnapshot(IOService *provider, n48pci::Snapshot &snapshot) {
    uint32_t value = 0;
    const bool present = PE_parse_boot_argn("navi48-pci-probe", &value, sizeof(value));
    // Check BEFORE even looking at the provider. Navi48Bringup's args cannot
    // enable this module. Only an explicit value of 1 opts in.
    if (!n48pci::enabled(present, value) || !OSDynamicCast(IOPCIDevice, provider))
        return false;
    n48pci::Snapshot candidate{};
    for (unsigned i = 0; i < n48pci::kIdentityCount; ++i) {
        OSObject *raw = provider->copyProperty(n48pci::kIdentityKeys[i]);
        const OSData *data = OSDynamicCast(OSData, raw);
        const bool valid = data && n48pci::word(data->getBytesNoCopy(), data->getLength(), candidate.identity[i]);
        if (raw) raw->release();
        if (!valid) return false;
    }
    if (!n48pci::target(candidate.identity)) return false;
    OSObject *raw = provider->copyProperty("assigned-addresses");
    const OSData *data = OSDynamicCast(OSData, raw);
    const bool valid = data && n48pci::resources(data->getBytesNoCopy(), data->getLength(), candidate) == n48pci::ParseResult::Ok;
    if (raw) raw->release();
    if (valid) snapshot = candidate;
    return valid;
}

bool putNumber(OSDictionary *dict, const char *key, uint64_t value, unsigned bits) {
    OSNumber *number = OSNumber::withNumber(value, bits);
    if (!number) return false;
    const bool ok = dict->setObject(key, number);
    number->release();
    return ok;
}

OSDictionary *makeReport(const n48pci::Snapshot &snapshot) {
    OSDictionary *report = OSDictionary::withCapacity(17);
    if (!report) return nullptr;
    bool ok = putNumber(report, "SchemaVersion", 1, 32);
    for (unsigned i = 0; ok && i < n48pci::kIdentityCount; ++i)
        ok = putNumber(report, n48pci::kIdentityKeys[i], snapshot.identity[i], 32);
    ok = ok && putNumber(report, "ResourceCount", snapshot.count, 32) &&
         putNumber(report, "PCIBDF", snapshot.bdf, 32);
    const char *names[n48pci::kMaxResources] = {"BAR0", "BAR1", "BAR2", "BAR3", "BAR4", "BAR5", "ROM"};
    for (unsigned i = 0; ok && i < snapshot.count; ++i) {
        const auto &r = snapshot.resources[i];
        OSDictionary *entry = OSDictionary::withCapacity(4);
        if (!entry) { ok = false; break; }
        ok = putNumber(entry, "ConfigRegister", r.reg, 32) &&
             putNumber(entry, "RegistryFlags", r.flags, 32) &&
             putNumber(entry, "Base", r.base, 64) &&
             putNumber(entry, "Length", r.length, 64) &&
             report->setObject(names[n48pci::slot(r.reg)], entry);
        entry->release();
    }
    if (!ok) { report->release(); return nullptr; }
    return report;
}
} // namespace

IOService *Navi48PciProbe::probe(IOService *provider, SInt32 *score) {
    n48pci::Snapshot snapshot{};
    if (!readSnapshot(provider, snapshot)) return nullptr;
    return IOService::probe(provider, score);
}

bool Navi48PciProbe::start(IOService *provider) {
    // Revalidate instead of trusting a previous probe's identity or properties.
    n48pci::Snapshot snapshot{};
    if (!readSnapshot(provider, snapshot)) return false;
    OSDictionary *report = makeReport(snapshot);
    if (!report) return false;
    if (!IOService::start(provider)) { report->release(); return false; }
    // A single property on OUR node, never on the provider. No Ready,
    // LoadAccelerator or Metal capabilities, no registered child services.
    const bool published = setProperty(kSnapshotKey, report);
    report->release();
    if (!published) { IOService::stop(provider); return false; }
    IOLog("Navi48PciProbe: registry-only snapshot, %u resources; no GPU initialization\n", snapshot.count);
    // Intentionally no registerService(): this observer offers no client service.
    return true;
}

void Navi48PciProbe::stop(IOService *provider) {
    removeProperty(kSnapshotKey);
    IOService::stop(provider);
}

IOReturn Navi48PciProbe::newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **handler) {
    if (handler) *handler = nullptr;
    return kIOReturnUnsupported;
}

IOReturn Navi48PciProbe::newUserClient(task_t, void *, UInt32, IOUserClient **handler) {
    if (handler) *handler = nullptr;
    return kIOReturnUnsupported;
}
