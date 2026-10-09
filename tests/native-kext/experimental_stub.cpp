// Lifecycle double ONLY. The real hardware engine is compiled with the SDK,
// not executed on host RAM or made to report a successful Radeon calculation.
#include "ExperimentalCompute.hpp"
namespace fakecompute {
unsigned calls = 0;
void (*onRun)() = nullptr;
}
namespace n48native {
ExperimentalCompute::~ExperimentalCompute() = default;
void ExperimentalCompute::cancel() { __atomic_store_n(&cancelled_, 1, __ATOMIC_RELEASE); }
bool ExperimentalCompute::hardwareTouched() const { return false; }
ExperimentalCompute::Snapshot ExperimentalCompute::snapshot() const { return facts_; }
IOReturn ExperimentalCompute::run(IOService *, IOPCIDevice *, IOWorkLoop *, const Input &) {
    ++fakecompute::calls;
    if (fakecompute::onRun) fakecompute::onRun();
    facts_.failedStage = 1;
    facts_.result = static_cast<uint32_t>(kIOReturnNotReady);
    return kIOReturnNotReady;
}
}
