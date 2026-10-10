// SPDX-License-Identifier: MIT
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include "IOKitController.hpp"
#include "DmaBuffer.hpp"
#include "ExperimentalCompute.hpp"

// Native PCI/DMA service, plus a separately permitted one-shot hardware trial.
// Old RO Claims/blockers remain intact. Never advertises an accelerator;
// experimental firmware/compute requires BOTH compute=1 and risk=1.
class Navi48Native final : public IOService {
    OSDeclareDefaultStructors(Navi48Native)
public:
    bool init(OSDictionary *dictionary = nullptr) override;
    void free() override;
    IOWorkLoop *getWorkLoop() const override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn message(UInt32 type, IOService *provider, void *argument = nullptr) override;
    bool willTerminate(IOService *provider, IOOptionBits options) override;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override;
    IOReturn newUserClient(task_t, void *, UInt32, IOUserClient **) override;
private:
    enum class Stage : uint32_t { Fresh, Starting, PreparingDma, Computing, TrialFinished, MappedFirmwareBlocked, Retired, Failed };
    // Stable IDs for the software-only IOResources boot diagnostic.
    enum class Checkpoint : uint32_t { InitBase = 1, WorkLoop, Gate, GateAdd,
        ProbeRequest, ProbeProvider, StartPreconditions, StartState, StartBase,
        PlatformAcquire, DmaFinalization, ComputeAllocation, ComputeFinished, PreparedOnly };
    struct Action {
        IOService *provider{nullptr};
        Checkpoint checkpoint{Checkpoint::StartPreconditions};
        n48native::PlatformSnapshot platformFacts{};
        n48native::PlatformResult platformDecision{n48native::PlatformResult::NotMapped};
        bool cancelledOrInactive{false};
        bool platformObserved{false}, dmaObserved{false}, computeObserved{false};
        n48native::PlatformRequest request{};
        bool stopBase{false};
        n48native::IOKitController *retiredController{nullptr};
        n48native::DmaBuffer *preparedDma{nullptr}, *retiredDma{nullptr};
        n48native::DmaBuffer::Snapshot dmaFacts{};
        IOReturn dmaResult{kIOReturnNotReady};
        bool runCompute{false}, computeRequested{false};
        n48native::ExperimentalCompute::Input computeInput{};
        n48native::ExperimentalCompute::Snapshot computeFacts{};
    };
    IOWorkLoop *workLoop_{nullptr};
    IOCommandGate *gate_{nullptr};
    n48native::IOKitController *controller_{nullptr};
    n48native::DmaBuffer *dma_{nullptr};
    n48native::ExperimentalCompute *compute_{nullptr};
    Stage stage_{Stage::Fresh};
    bool gateAdded_{false}, baseStarted_{false}, attempted_{false}, cancelStart_{false};

    static IOReturn startAction(OSObject *, void *, void *, void *, void *);
    static IOReturn retireAction(OSObject *, void *, void *, void *, void *);
    static IOReturn finishAction(OSObject *, void *, void *, void *, void *);
    IOReturn startGated(Action &);
    IOReturn finishGated(Action &);
    static IOReturn computeFinishAction(OSObject *, void *, void *, void *, void *);
    IOReturn computeFinishGated(Action &);
    void withdrawGated(Action &, bool stopBase);
    void dispose(Action &); // foreign close/base-stop after leaving the gate
    void invalidate();
    static void recordBootDiagnostic(const Action &, IOReturn); // OUTSIDE gate, no owner/provider retained
    bool publish(const n48native::PlatformSnapshot &, const n48native::DmaBuffer::Snapshot &);
};
