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
    struct Snapshot {
        uint32_t stage{0}, failedStage{0}, result{static_cast<uint32_t>(kIOReturnNotReady)};
        bool hardwareTouched{false}, firmwareLoaded{false}, initialized{false}, computePassed{false}, idleSleepPrevented{false};
        uint64_t vramBytes{0}, mcBase{0}, elapsedUs[2]{};
        bool fenceLanded[2]{}, ibTestPassed[2]{};
        uint32_t dmaBuffers{0}, lanesChecked{0}, lanesWrong{0}, observed[2][4]{}, expected[2][4]{};
    };
    ExperimentalCompute() = default;
    ~ExperimentalCompute();
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
