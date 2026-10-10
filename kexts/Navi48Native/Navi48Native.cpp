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
bool computeOptIn(bool &enabled) {
    uint32_t compute = 0, risk = 0;
    const bool c = PE_parse_boot_argn("navi48-native-compute", &compute, sizeof(compute));
    const bool r = PE_parse_boot_argn("navi48-native-risk", &risk, sizeof(risk));
    enabled = c && r && compute == 1 && risk == 1;
    return enabled || ((!c || compute == 0) && (!r || risk == 0));
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
    Action diagnostic{};
    diagnostic.checkpoint = Checkpoint::InitBase;
    if (!IOService::init(dictionary)) { recordBootDiagnostic(diagnostic, kIOReturnNotReady); return false; }
    workLoop_ = IOWorkLoop::workLoop();
    diagnostic.checkpoint = Checkpoint::WorkLoop;
    if (!workLoop_) { recordBootDiagnostic(diagnostic, kIOReturnNoMemory); return false; }
    gate_ = IOCommandGate::commandGate(this);
    diagnostic.checkpoint = Checkpoint::Gate;
    if (!gate_) { recordBootDiagnostic(diagnostic, kIOReturnNoMemory); return false; }
    diagnostic.checkpoint = Checkpoint::GateAdd;
    const IOReturn added = workLoop_->addEventSource(gate_);
    if (added != kIOReturnSuccess) { recordBootDiagnostic(diagnostic, added); return false; }
    gateAdded_ = true;
    return true;
}
IOWorkLoop *Navi48Native::getWorkLoop() const { return workLoop_; }
void Navi48Native::free() {
    // IOKit must have drained external calls before free. stop/failure normally
    // already withdrew everything; this also handles partially initialized objects.
    if (gateAdded_) stop(getProvider());
    if (compute_) { delete compute_; compute_ = nullptr; }
    if (gateAdded_) workLoop_->removeEventSource(gate_);
    gateAdded_ = false;
    if (gate_) gate_->release();
    if (workLoop_) workLoop_->release();
    gate_ = nullptr; workLoop_ = nullptr;
    IOService::free();
}
IOService *Navi48Native::probe(IOService *provider, SInt32 *score) {
    Action diagnostic{};
    diagnostic.checkpoint = Checkpoint::ProbeRequest;
    if (!readRequest(diagnostic.request)) { recordBootDiagnostic(diagnostic, kIOReturnBadArgument); return nullptr; }
    diagnostic.checkpoint = Checkpoint::ProbeProvider;
    if (!OSDynamicCast(IOPCIDevice, provider)) { recordBootDiagnostic(diagnostic, kIOReturnBadArgument); return nullptr; }
    return IOService::probe(provider, score);
}
bool Navi48Native::start(IOService *provider) {
    Action action{};
    action.provider = provider;
    if (!gateAdded_ || !readRequest(action.request) || !computeOptIn(action.computeRequested) ||
        !OSDynamicCast(IOPCIDevice, provider)) { recordBootDiagnostic(action, kIOReturnBadArgument); return false; }
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
        action.dmaObserved = true;
        result = gate->runAction(finishAction, &action);
        if (result == kIOReturnSuccess && action.runCompute) {
            (void)compute_->run(this, OSDynamicCast(IOPCIDevice, provider), loop, action.computeInput);
            action.computeFacts = compute_->snapshot(); action.computeObserved = true;
            result = gate->runAction(computeFinishAction, &action);
        }
    }
    dispose(action); // DMA first, then BAR mappings/PCI close, ALL outside gate
    recordBootDiagnostic(action, result); // value-only record survives failed start()/free()
    gate->release(); loop->release(); provider->release(); release();
    return result == kIOReturnSuccess;
}
IOReturn Navi48Native::startAction(OSObject *owner, void *argument, void *, void *, void *) {
    return static_cast<Navi48Native *>(owner)->startGated(*static_cast<Action *>(argument));
}
IOReturn Navi48Native::startGated(Action &action) {
    action.checkpoint = Checkpoint::StartState;
    if (attempted_ || stage_ != Stage::Fresh) return kIOReturnNotReady;
    attempted_ = true; stage_ = Stage::Starting; cancelStart_ = false;
    action.checkpoint = Checkpoint::StartBase;
    if (!IOService::start(action.provider)) { stage_ = Stage::Failed; return kIOReturnNotReady; }
    baseStarted_ = true;
    controller_ = new n48native::IOKitController;
    auto result = n48native::PlatformResult::NoLock;
    if (controller_ && !cancelStart_) result = controller_->acquire(this, action.provider, action.request);
    n48native::PlatformSnapshot snapshot{};
    if (controller_) snapshot = controller_->snapshot();
    action.checkpoint = Checkpoint::PlatformAcquire;
    action.platformFacts = snapshot; action.platformObserved = controller_ != nullptr;
    action.platformDecision = result; action.cancelledOrInactive = cancelStart_ || isInactive();
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
    action.checkpoint = Checkpoint::DmaFinalization;
    auto result = n48native::PlatformResult::NotMapped;
    if (stage_ == Stage::PreparingDma && controller_ && !cancelStart_ && !isInactive() &&
        action.dmaResult == kIOReturnSuccess && action.dmaFacts.phase == n48native::DmaBuffer::Phase::Prepared)
        result = controller_->revalidateHeld();
    n48native::PlatformSnapshot snapshot{};
    if (controller_) snapshot = controller_->snapshot();
    action.platformFacts = snapshot; action.platformObserved = controller_ != nullptr; action.platformDecision = result;
    if (result != n48native::PlatformResult::MappingsHeldUnqualified || cancelStart_ || isInactive() ||
        !publish(snapshot, action.dmaFacts) || cancelStart_ || isInactive()) {
        action.cancelledOrInactive = cancelStart_ || isInactive();
        IOLog("Navi48Native: DMA/final validation refused dma=0x%x platform=%u cancelled=%u; no GPU submission\n",
              action.dmaResult, static_cast<unsigned>(result), cancelStart_ ? 1u : 0u);
        stage_ = Stage::Failed;
        withdrawGated(action, true);
        return kIOReturnNotReady;
    }
    dma_ = action.preparedDma; action.preparedDma = nullptr;
    if (action.computeRequested) {
        action.checkpoint = Checkpoint::ComputeAllocation;
        compute_ = new n48native::ExperimentalCompute;
        if (!compute_) { stage_ = Stage::Failed; withdrawGated(action, true); return kIOReturnNoMemory; }
        auto &input = action.computeInput;
        input.offset = snapshot.candidateInBar0.offset; input.bytes = snapshot.candidateInBar0.bytes;
        for (unsigned i = 0; i < 3; ++i) { input.barPhysical[i] = snapshot.bars[i].cpuPhysical; input.barBytes[i] = snapshot.bars[i].bytes; }
        input.consoleBase = snapshot.console.rawBase; input.rowBytes = snapshot.console.rowBytes;
        input.width = snapshot.console.width; input.height = snapshot.console.height; input.depth = snapshot.console.depth;
        input.consoleOffset = snapshot.console.offset; input.consoleLength = snapshot.console.length;
        action.runCompute = true; stage_ = Stage::Computing;
        return kIOReturnSuccess;
    }
    stage_ = Stage::MappedFirmwareBlocked; action.checkpoint = Checkpoint::PreparedOnly;
    IOLog("Navi48Native: started 0.2.2 bdf=0x%x DMA prepared bytes=%llu pages=%u deviceMapper=%u blockers=0x%x; GPU initialization blocked\n",
          snapshot.bdf, static_cast<unsigned long long>(action.dmaFacts.bytes), action.dmaFacts.pages,
          action.dmaFacts.deviceMapper ? 1u : 0u, snapshot.blockers);
    // Actual RAM allocation + IOVM generation, NOT GPU DMA qualification.
    // Never markPublished(), write BAR contents, invoke firmware or register an
    // accelerator using fabricated Claims or a guessed writable VRAM interval.
    return kIOReturnSuccess;
}

IOReturn Navi48Native::computeFinishAction(OSObject *owner, void *argument, void *, void *, void *) {
    return static_cast<Navi48Native *>(owner)->computeFinishGated(*static_cast<Action *>(argument));
}
IOReturn Navi48Native::computeFinishGated(Action &action) {
    action.checkpoint = Checkpoint::ComputeFinished;
    const auto &f = action.computeFacts;
    auto *report = OSDictionary::withCapacity(24);
    bool ok = report && number(report, "SchemaVersion", 1, 32) && number(report, "Stage", f.stage, 32) &&
        number(report, "FailedStage", f.failedStage, 32) && number(report, "PreflightCheck", f.preflightCheck, 32) && number(report, "Result", f.result, 32) &&
        number(report, "HardwareTouched", f.hardwareTouched, 32) && number(report, "FirmwareLoaded", f.firmwareLoaded, 32) &&
        number(report, "GPUInitialized", f.initialized, 32) && number(report, "ComputePassed", f.computePassed, 32) &&
        number(report, "VRAMBytes", f.vramBytes) && number(report, "MCBase", f.mcBase) &&
        number(report, "DMABuffers", f.dmaBuffers, 32) && number(report, "LanesChecked", f.lanesChecked, 32) &&
        number(report, "LanesWrong", f.lanesWrong, 32) && number(report, "ResourcesRetainedUntilReboot", f.hardwareTouched, 32) &&
        number(report, "IdleSleepAssertion", f.idleSleepPrevented, 32) && number(report, "DemandSleepSupported", 0, 32) &&
        number(report, "HardwareQualificationComplete", 0, 32) && number(report, "MetalAcceleration", 0, 32);
    for (unsigned variant = 0; ok && variant < 2; ++variant) {
        auto *values = OSDictionary::withCapacity(11);
        if (!values) { ok = false; break; }
        ok = number(values, "ElapsedUs", f.elapsedUs[variant]) && number(values, "FenceLanded", f.fenceLanded[variant], 32) &&
            number(values, "IBTestPassed", f.ibTestPassed[variant], 32);
        constexpr const char *observed[4] = {"Observed0", "Observed1", "Observed2", "Observed3"};
        constexpr const char *expected[4] = {"Expected0", "Expected1", "Expected2", "Expected3"};
        for (unsigned i = 0; ok && i < 4; ++i) ok = number(values, observed[i], f.observed[variant][i], 32) && number(values, expected[i], f.expected[variant][i], 32);
        if (ok) ok = report->setObject(variant ? "LLVMShader" : "AssemblyShader", values);
        values->release();
    }
    if (ok) ok = setProperty("Navi48Native,Compute", report);
    if (report) report->release();
    stage_ = Stage::TrialFinished; action.cancelledOrInactive = cancelStart_ || isInactive();
    if (!f.hardwareTouched) { stage_ = Stage::Failed; withdrawGated(action, true); return kIOReturnNotReady; }
    // Once GPU-visible, retain the service/module, RAM, mappings and PCI lease
    // even when a stage times out or registry publication fails. NO hot unload.
    if (!ok) IOLog("Navi48Native: compute report publication failed; GPU-visible resources retained\n");
    if (cancelStart_ || isInactive()) withdrawGated(action, true);
    else registerService();
    return kIOReturnSuccess; // service retains diagnostic FAILURE as well as success
}
void Navi48Native::recordBootDiagnostic(const Action &a, IOReturn result) {
    // Generic values ONLY in IOResources: no owner/provider/map references.
    // Survives detach/free and a wrapped dmesg buffer. Best effort on OOM;
    // NEVER changes the result, authorizes accesses or retries hardware.
    if (a.checkpoint == Checkpoint::StartState) return; // reject rearm without overwriting original result
    auto *report = OSDictionary::withCapacity(64);
    if (!report) { IOLog("Navi48Native: boot diagnostic allocation failed\n"); return; }
    const auto &p = a.platformFacts; const auto &c = a.computeFacts;
    bool ok = number(report, "SchemaVersion", 1, 32) && number(report, "DriverVersion", 0x000202, 32) &&
        number(report, "Checkpoint", static_cast<uint32_t>(a.checkpoint), 32) &&
        number(report, "StartReturn", static_cast<uint32_t>(result), 32) &&
        number(report, "ComputeRequested", a.computeRequested, 32) &&
        number(report, "PlatformObserved", a.platformObserved, 32) && number(report, "DMAObserved", a.dmaObserved, 32) &&
        number(report, "ComputeObserved", a.computeObserved, 32) &&
        number(report, "PlatformResult", static_cast<uint32_t>(p.result), 32) &&
        number(report, "PlatformDecision", static_cast<uint32_t>(a.platformDecision), 32) &&
        number(report, "CancelledOrInactive", a.cancelledOrInactive, 32) &&
        number(report, "PlatformPhase", static_cast<uint32_t>(p.phase), 32) && number(report, "FailedBar", p.failedBar, 32) &&
        number(report, "PCIBDF", p.bdf, 32) && number(report, "PCICommand", p.command, 32) &&
        number(report, "MappingsHeldBeforeCleanup", p.mappingsHeld, 32) &&
        number(report, "ConsoleResult", static_cast<uint32_t>(p.console.result), 32) &&
        number(report, "ConsoleBase", p.console.rawBase) && number(report, "ConsoleRowBytes", p.console.rowBytes) &&
        number(report, "ConsoleWidth", p.console.width) && number(report, "ConsoleHeight", p.console.height) &&
        number(report, "ConsoleDepth", p.console.depth) && number(report, "ConsoleOffset", p.console.offset) && number(report, "ConsoleLength", p.console.length) &&
        number(report, "ScratchOffset", a.request.candidate.offset) && number(report, "ScratchBytes", a.request.candidate.bytes) &&
        number(report, "DMAResult", static_cast<uint32_t>(a.dmaResult), 32) &&
        number(report, "DMAPhase", static_cast<uint32_t>(a.dmaFacts.phase), 32) && number(report, "DMAPages", a.dmaFacts.pages, 32) &&
        number(report, "ComputeStage", c.stage, 32) && number(report, "FailedStage", c.failedStage, 32) &&
        number(report, "PreflightCheck", c.preflightCheck, 32) && number(report, "ComputeResult", c.result, 32) &&
        number(report, "HardwareTouched", c.hardwareTouched, 32) && number(report, "FirmwareLoaded", c.firmwareLoaded, 32) &&
        number(report, "GPUInitialized", c.initialized, 32) &&
        number(report, "ComputePassed", c.computePassed, 32) && number(report, "LanesChecked", c.lanesChecked, 32) &&
        number(report, "LanesWrong", c.lanesWrong, 32) && recordBars(report, a.platformFacts);
    if (ok) IOService::publishResource("Navi48Native,BootDiagnostics", report); // outside gate, after cleanup
    else IOLog("Navi48Native: boot diagnostic serialization failed\n");
    report->release();
}
bool Navi48Native::recordBars(OSDictionary *report, const n48native::PlatformSnapshot &snapshot) {
    // Decoded per-BAR mapping observations. Values only: addresses/options are
    // never dereferenced, and no owner/provider/map is retained. NotChecked for
    // a BAR that checkMaps() never reached; later BARs keep their zero defaults.
    constexpr const char *names[3] = {"BAR0Map", "BAR2Map", "BAR5Map"};
    for (unsigned i = 0; i < 3; ++i) {
        auto *bar = OSDictionary::withCapacity(8);
        if (!bar) return false;
        const auto &observation = snapshot.bars[i];
        const bool ok = number(bar, "MapCheck", static_cast<uint32_t>(observation.mapCheck), 32) &&
            number(bar, "ObservedAddress", observation.observedAddress) &&
            number(bar, "ObservedMapOptions", observation.observedMapOptions, 32) &&
            number(bar, "ObservedLength", observation.observedLength) &&
            number(bar, "ObservedContiguous", observation.observedContiguous) &&
            number(bar, "ObservedPhysical", observation.observedPhysical) &&
            number(bar, "DescriptorMatch", observation.observedDescriptorMatch, 32) &&
            number(bar, "TaskMatch", observation.observedTaskMatch, 32) && report->setObject(names[i], bar);
        bar->release();
        if (!ok) return false;
    }
    return true;
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
        number(report, "DMAResult", static_cast<uint32_t>(dma.result), 32) && number(report, "DMABytes", dma.bytes) &&
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
    if (compute_ && compute_->hardwareTouched()) {
        compute_->cancel();
        action.stopBase = stopBase && baseStarted_;
        if (stopBase) baseStarted_ = false;
        removeProperty(kResources); // diagnostics never advertise an active lease after stop
        return; // terminal owner cycle retains every GPU-visible object until reboot
    }
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
    if (self->stage_ == Stage::Starting || self->stage_ == Stage::PreparingDma || self->stage_ == Stage::Computing) {
        if (self->compute_) self->compute_->cancel();
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
