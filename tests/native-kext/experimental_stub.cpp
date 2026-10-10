// Lifecycle double ONLY. The real hardware engine is compiled with the SDK,
// not executed on host RAM or made to report a successful Radeon calculation.
#include "ExperimentalCompute.hpp"
namespace fakecompute {
unsigned calls = 0;
bool simulatePublication = false;
void (*onRun)() = nullptr;
} // namespace fakecompute
namespace n48native {
ExperimentalCompute::~ExperimentalCompute() = default;
void ExperimentalCompute::cancel() { __atomic_store_n(&cancelled_, 1, __ATOMIC_RELEASE); }
bool ExperimentalCompute::hardwareTouched() const { return facts_.hardwareTouched; }
ExperimentalCompute::Snapshot ExperimentalCompute::snapshot() const { return facts_; }
IOReturn ExperimentalCompute::run(IOService *owner, IOPCIDevice *pci, IOWorkLoop *loop, const Input &) {
    ++fakecompute::calls;
    if (fakecompute::onRun) fakecompute::onRun();
    facts_.hardwareTouched = fakecompute::simulatePublication; // lifetime model ONLY
    if (facts_.hardwareTouched) { owner->retain(); pci->retain(); loop->retain(); } // boot-long model, no GPU work
    facts_.failedStage = 1;
    facts_.preflightCheck = 3; // model-only console geometry refusal
    facts_.result = static_cast<uint32_t>(kIOReturnNotReady);
    return kIOReturnNotReady;
}
}
