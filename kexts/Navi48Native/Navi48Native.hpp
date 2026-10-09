// SPDX-License-Identifier: MIT
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include "IOKitController.hpp"

// Stage-1 native driver. Owns the real PCI resource controller; never advertises
// an accelerator or executes firmware while the controller has hardware blockers.
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
    enum class Stage : uint32_t { Fresh, Starting, MappedFirmwareBlocked, Retired, Failed };
    struct Action {
        IOService *provider{nullptr};
        n48native::PlatformRequest request{};
        bool stopBase{false};
        n48native::IOKitController *retiredController{nullptr};
    };
    IOWorkLoop *workLoop_{nullptr};
    IOCommandGate *gate_{nullptr};
    n48native::IOKitController *controller_{nullptr};
    Stage stage_{Stage::Fresh};
    bool gateAdded_{false}, baseStarted_{false}, attempted_{false}, cancelStart_{false};

    static IOReturn startAction(OSObject *, void *, void *, void *, void *);
    static IOReturn retireAction(OSObject *, void *, void *, void *, void *);
    IOReturn startGated(Action &);
    void withdrawGated(Action &, bool stopBase);
    void dispose(Action &); // foreign close/base-stop after leaving the gate
    void invalidate();
    bool publish(const n48native::PlatformSnapshot &);
};
