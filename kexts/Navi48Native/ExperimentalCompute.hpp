// SPDX-License-Identifier: MIT
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
namespace n48native {
// A distinct EXPLICIT hardware trial. No fabricated Claims/qualification.
class ExperimentalCompute final {
public:
    struct Input {
        uint64_t offset{0}, bytes{0}, barPhysical[3]{}, barBytes[3]{};
        uint64_t consoleBase{0}, rowBytes{0}, width{0}, height{0}, depth{0}, consoleOffset{0}, consoleLength{0};
    };
    // Field-level RW mapping observation, mirroring the RO MapCheck IDs.
    // Values only; no pointer retained. NotChecked for a BAR the RW loop
    // never reached.
    enum class RwMapCheck : uint32_t {
        NotChecked = 0, Ok = 1, NullMap = 2, DescriptorMismatch = 3, TaskMismatch = 4,
        LengthMismatch = 5, ContiguousMismatch = 6, PhysicalMismatch = 7, CacheMismatch = 8,
        FlagsMismatch = 9, NullAddress = 10, UnalignedAddress = 11, NullDescriptor = 12,
        DescriptorLengthMismatch = 13
    };
    struct RwBarObservation {
        RwMapCheck check{RwMapCheck::NotChecked};
        uint64_t observedAddress{0};
        IOOptionBits observedMapOptions{0};
        uint64_t observedLength{0}, observedContiguous{0}, observedPhysical{0};
        bool observedDescriptorMatch{false}, observedTaskMatch{false};
        bool observedRereadPresent{false}, observedRereadMatch{false}, observedDeclaredIsReread{false};
    };
    struct Snapshot {
        uint32_t stage{0}, failedStage{0}, preflightCheck{0}, result{static_cast<uint32_t>(kIOReturnNotReady)};
        bool hardwareTouched{false}, firmwareLoaded{false}, initialized{false}, computePassed{false}, idleSleepPrevented{false};
        bool acceleratorIteratorNull{false}; // true when getMatchingServices() returned null (empty set presumed)
        RwBarObservation rwBars[3]{};
        uint64_t vramBytes{0}, mcBase{0}, elapsedUs[2]{};
        bool fenceLanded[2]{}, ibTestPassed[2]{};
        uint32_t dmaBuffers{0}, lanesChecked{0}, lanesWrong{0}, observed[2][4]{}, expected[2][4]{};
    };
    ExperimentalCompute() = default;
    ~ExperimentalCompute();
    // Accelerator-exclusion preflight (AcceleratorPreflight.hpp): the exact
    // function run() calls, host-tested with IOKit doubles. Static wrapper
    // keeps the call site qualified while sharing one implementation.
    static IOReturn checkNoAccelerator(bool &iteratorNull);
    IOReturn run(IOService *, IOPCIDevice *, IOWorkLoop *, const Input &);
    void cancel();
    Snapshot snapshot() const; // only after run exits, serialized by the service
    bool hardwareTouched() const;
private:
    struct Resources;
    Resources *resources_{nullptr};
    int32_t cancelled_{0};
    Snapshot facts_{};
};
}
