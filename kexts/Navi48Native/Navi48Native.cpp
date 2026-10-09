// SPDX-License-Identifier: MIT
#include "Navi48Native.hpp"
#include <IOKit/IOMessage.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

OSDefineMetaClassAndStructors(Navi48Native, IOService)

namespace {
constexpr const char *kResources = "Navi48Native,Resources";
constexpr uint64_t kPspLayoutBytes = 24 * 1024 * 1024;
constexpr uint64_t kDmaBytes = 64 * 1024; // system RAM, independent of the BAR0 candidate
bool readRequest(n48native::PlatformRequest &out) {
    out = {};
    uint32_t enabled = 0;
    uint64_t offset = 0, bytes = 0;
    // No implicit scratch placement, no bringup/probe argument can opt us in.
    if (!PE_parse_boot_argn("navi48-native-platform", &enabled, sizeof(enabled)) || enabled != 1 ||
        !PE_parse_boot_argn("navi48-native-scratch-offset", &offset, sizeof(offset)) ||
        !PE_parse_boot_argn("navi48-native-scratch-bytes", &bytes, sizeof(bytes)) ||
        (offset & 0xffff) || (bytes & 0xfff) || bytes < kPspLayoutBytes || offset > UINT64_MAX - bytes)
        return false;
    out = {{offset, bytes}, kPspLayoutBytes}; // candidate only; NOT a VRAM ownership proof
    return true;
}
bool number(OSDictionary *dictionary, const char *key, uint64_t value, unsigned bits = 64) {
    auto *entry = OSNumber::withNumber(value, bits);
    if (!entry) return false;
    const bool ok = dictionary->setObject(key, entry);
    entry->release();
    return ok;
}
} // namespace

bool Navi48Native::init(OSDictionary *dictionary) {
    if (!IOService::init(dictionary)) return false;
    workLoop_ = IOWorkLoop::workLoop();
    if (!workLoop_) return false;
    gate_ = IOCommandGate::commandGate(this);
    if (!gate_) return false;
    if (workLoop_->addEventSource(gate_) != kIOReturnSuccess) return false;
    gateAdded_ = true;
    return true;
}
IOWorkLoop *Navi48Native::getWorkLoop() const { return workLoop_; }
void Navi48Native::free() {
    // IOKit must have drained external calls before free. stop/failure normally
    // already withdrew everything; this also handles partially initialized objects.
    if (gateAdded_) stop(getProvider());
    if (gateAdded_) workLoop_->removeEventSource(gate_);
    gateAdded_ = false;
    if (gate_) gate_->release();
    if (workLoop_) workLoop_->release();
    gate_ = nullptr; workLoop_ = nullptr;
    IOService::free();
}
IOService *Navi48Native::probe(IOService *provider, SInt32 *score) {
    n48native::PlatformRequest request{};
    if (!readRequest(request) || !OSDynamicCast(IOPCIDevice, provider)) return nullptr;
    return IOService::probe(provider, score);
}
bool Navi48Native::start(IOService *provider) {
    Action action{};
    action.provider = provider;
    if (!gateAdded_ || !readRequest(action.request) || !OSDynamicCast(IOPCIDevice, provider)) return false;
    // IODMACommand::prepare/complete can block and call back into this service.
    // Keep the service, provider and workloop alive across the unlocked phase.
    retain(); provider->retain();
    auto *loop = workLoop_; auto *gate = gate_;
    loop->retain(); gate->retain();
    IOReturn result = gate->runAction(startAction, &action);
    if (result == kIOReturnSuccess) {
        action.preparedDma = new n48native::DmaBuffer;
        action.dmaResult = action.preparedDma ? action.preparedDma->allocate(this,
            OSDynamicCast(IOPCIDevice, provider), loop, kDmaBytes) : kIOReturnNoMemory;
        if (action.preparedDma) action.dmaFacts = action.preparedDma->snapshot();
        result = gate->runAction(finishAction, &action);
    }
    dispose(action); // DMA first, then BAR mappings/PCI close, ALL outside gate
    gate->release(); loop->release(); provider->release(); release();
    return result == kIOReturnSuccess;
}
IOReturn Navi48Native::startAction(OSObject *owner, void *argument, void *, void *, void *) {
    return static_cast<Navi48Native *>(owner)->startGated(*static_cast<Action *>(argument));
}
IOReturn Navi48Native::startGated(Action &action) {
    if (attempted_ || stage_ != Stage::Fresh) return kIOReturnNotReady;
    attempted_ = true; stage_ = Stage::Starting; cancelStart_ = false;
    if (!IOService::start(action.provider)) { stage_ = Stage::Failed; return kIOReturnNotReady; }
    baseStarted_ = true;
    controller_ = new n48native::IOKitController;
    auto result = n48native::PlatformResult::NoLock;
    if (controller_ && !cancelStart_) result = controller_->acquire(this, action.provider, action.request);
    n48native::PlatformSnapshot snapshot{};
    if (controller_) snapshot = controller_->snapshot();
    if (result != n48native::PlatformResult::MappingsHeldUnqualified || cancelStart_ || isInactive()) {
        IOLog("Navi48Native: start refused result=%u bar=0x%x; firmware not executed\n",
              static_cast<unsigned>(result), snapshot.failedBar);
        stage_ = Stage::Failed;
        withdrawGated(action, true);
        return kIOReturnNotReady;
    }
    stage_ = Stage::PreparingDma;
    return kIOReturnSuccess; // continue start() OUTSIDE the workloop gate
}
IOReturn Navi48Native::finishAction(OSObject *owner, void *argument, void *, void *, void *) {
    return static_cast<Navi48Native *>(owner)->finishGated(*static_cast<Action *>(argument));
}
IOReturn Navi48Native::finishGated(Action &action) {
    auto result = n48native::PlatformResult::NotMapped;
    if (stage_ == Stage::PreparingDma && controller_ && !cancelStart_ && !isInactive() &&
        action.dmaResult == kIOReturnSuccess && action.dmaFacts.phase == n48native::DmaBuffer::Phase::Prepared)
        result = controller_->revalidateHeld();
    n48native::PlatformSnapshot snapshot{};
    if (controller_) snapshot = controller_->snapshot();
    if (result != n48native::PlatformResult::MappingsHeldUnqualified || cancelStart_ || isInactive() ||
        !publish(snapshot, action.dmaFacts) || cancelStart_ || isInactive()) {
        IOLog("Navi48Native: DMA/final validation refused dma=0x%x platform=%u cancelled=%u; no GPU submission\n",
              action.dmaResult, static_cast<unsigned>(result), cancelStart_ ? 1u : 0u);
        stage_ = Stage::Failed;
        withdrawGated(action, true);
        return kIOReturnNotReady;
    }
    dma_ = action.preparedDma; action.preparedDma = nullptr;
    stage_ = Stage::MappedFirmwareBlocked;
    IOLog("Navi48Native: started 0.1.2 bdf=0x%x DMA prepared bytes=%llu pages=%u deviceMapper=%u blockers=0x%x; GPU initialization blocked\n",
          snapshot.bdf, static_cast<unsigned long long>(action.dmaFacts.bytes), action.dmaFacts.pages,
          action.dmaFacts.deviceMapper ? 1u : 0u, snapshot.blockers);
    // Actual RAM allocation + IOVM generation, NOT GPU DMA qualification.
    // Never markPublished(), write BAR contents, invoke firmware or register an
    // accelerator using fabricated Claims or a guessed writable VRAM interval.
    return kIOReturnSuccess;
}

bool Navi48Native::publish(const n48native::PlatformSnapshot &snapshot, const n48native::DmaBuffer::Snapshot &dma) {
    auto *report = OSDictionary::withCapacity(24);
    if (!report) return false;
    bool ok = number(report, "SchemaVersion", 2, 32) &&
        number(report, "PlatformPhase", static_cast<uint32_t>(snapshot.phase), 32) &&
        number(report, "PlatformResult", static_cast<uint32_t>(snapshot.result), 32) &&
        number(report, "PCIBDF", snapshot.bdf, 32) && number(report, "PCICommand", snapshot.command, 32) &&
        number(report, "Blockers", snapshot.blockers, 32) &&
        number(report, "MappingsHeld", snapshot.mappingsHeld, 32) &&
        number(report, "ObservationsValid", snapshot.observationsValid, 32) &&
        number(report, "AccessEnabled", snapshot.accessEnabled, 32) &&
        number(report, "FirmwareCoreLinked", 1, 32) && number(report, "FirmwareExecuted", 0, 32) &&
        number(report, "GPUInitialized", 0, 32) &&
        number(report, "DMAAllocatorInvoked", 1, 32) &&
        number(report, "DMAPhase", static_cast<uint32_t>(dma.phase), 32) &&
        number(report, "DMAResult", dma.result, 32) && number(report, "DMABytes", dma.bytes) &&
        number(report, "DMAPages", dma.pages, 32) && number(report, "DMADeviceMapper", dma.deviceMapper, 32) &&
        number(report, "DMAAddressPublished", 0, 32) && number(report, "GPUDMAValidated", 0, 32) &&
        number(report, "CandidateBAR0Offset", snapshot.candidateInBar0.offset) &&
        number(report, "CandidateBytes", snapshot.candidateInBar0.bytes) &&
        number(report, "ConsoleResult", static_cast<uint32_t>(snapshot.console.result), 32) &&
        number(report, "ConsoleRawBase", snapshot.console.rawBase) &&
        number(report, "ConsoleBAR0Offset", snapshot.console.allocationInBar0.offset) &&
        number(report, "ConsoleAllocationBytes", snapshot.console.allocationInBar0.bytes);
    constexpr const char *names[3] = {"BAR0", "BAR2", "BAR5"};
    for (unsigned i = 0; ok && i < 3; ++i) {
        auto *bar = OSDictionary::withCapacity(4);
        if (!bar) { ok = false; break; }
        const auto &observation = snapshot.bars[i];
        ok = number(bar, "ConfigRegister", observation.configRegister, 32) &&
             number(bar, "CPUPhysical", observation.cpuPhysical) && number(bar, "Bytes", observation.bytes) &&
             number(bar, "ReportedMapOptions", observation.reportedMapOptions, 32) && report->setObject(names[i], bar);
        bar->release();
    }
    if (ok) ok = setProperty(kResources, report); // OUR node only; never the provider
    report->release();
    return ok;
}
void Navi48Native::withdrawGated(Action &action, bool stopBase) {
    action.retiredDma = dma_; dma_ = nullptr;
    action.retiredController = controller_; controller_ = nullptr;
    action.stopBase = stopBase && baseStarted_;
    if (stopBase) baseStarted_ = false; // before any foreign close can reenter stop
    removeProperty(kResources); // no stale "MappingsHeld" report after withdrawal
}
void Navi48Native::dispose(Action &action) {
    // No buffer address was published: safe cleanup of this prepared allocation.
    // On uncertain complete/clear DmaBuffer retains a terminal quarantine instead.
    if (action.preparedDma) { delete action.preparedDma; action.preparedDma = nullptr; }
    if (action.retiredDma) { delete action.retiredDma; action.retiredDma = nullptr; }
    if (action.retiredController) {
        action.retiredController->release();
        delete action.retiredController;
        action.retiredController = nullptr;
    }
    if (action.stopBase) { action.stopBase = false; IOService::stop(action.provider); }
}
IOReturn Navi48Native::retireAction(OSObject *owner, void *argument, void *stopBase, void *, void *) {
    auto *self = static_cast<Navi48Native *>(owner);
    if (self->stage_ == Stage::Starting || self->stage_ == Stage::PreparingDma) {
        self->cancelStart_ = true; // finishAction owns rollback, including base stop
        return kIOReturnSuccess;
    }
    if (self->stage_ != Stage::Failed) self->stage_ = Stage::Retired;
    self->attempted_ = true;
    self->withdrawGated(*static_cast<Action *>(argument), stopBase != nullptr);
    return kIOReturnSuccess;
}
void Navi48Native::stop(IOService *provider) {
    if (!gateAdded_) return;
    Action action{}; action.provider = provider;
    gate_->runAction(retireAction, &action, this);
    dispose(action);
}
void Navi48Native::invalidate() {
    if (!gateAdded_) return;
    Action action{}; action.provider = getProvider();
    gate_->runAction(retireAction, &action);
    dispose(action);
    IOLog("Navi48Native: resource retirement requested; no automatic GPU resume\n");
}
IOReturn Navi48Native::message(UInt32 type, IOService *provider, void *argument) {
    if (provider == getProvider() && (type == kIOMessageServiceIsTerminated ||
        type == kIOMessageServiceIsRequestingClose || type == kIOMessageServiceIsSuspended ||
        type == kIOMessageDeviceWillPowerOff)) {
        invalidate();
        return kIOReturnSuccess;
    }
    return IOService::message(type, provider, argument);
}
bool Navi48Native::willTerminate(IOService *provider, IOOptionBits options) {
    if (provider == getProvider()) invalidate();
    return IOService::willTerminate(provider, options);
}
IOReturn Navi48Native::newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **handler) {
    if (handler) *handler = nullptr;
    return kIOReturnUnsupported;
}
IOReturn Navi48Native::newUserClient(task_t, void *, UInt32, IOUserClient **handler) {
    if (handler) *handler = nullptr;
    return kIOReturnUnsupported;
}
