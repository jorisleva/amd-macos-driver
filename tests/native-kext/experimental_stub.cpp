// Lifecycle double ONLY. The real hardware engine is compiled with the SDK,
// not executed on host RAM or made to report a successful Radeon calculation.
#include "ExperimentalCompute.hpp"
#include "Navi48Shell.hpp"
#include "Navi48Native.hpp"
// Read-only shell double: open/start succeed only when the test arms them;
// dispatch always refuses (no hardware behind it). Covers the gate/refusal
// plumbing; real reads are kext-build only.
bool Navi48Shell::initWithTask(task_t, void *, UInt32 type) { return type == 0; }
bool Navi48Shell::start(IOService *provider) {
    auto *service = OSDynamicCast(Navi48Native, provider);
    return service && service->shellAvailable();
}
IOReturn Navi48Shell::clientClose() { return kIOReturnSuccess; }
IOReturn Navi48Shell::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                    IOExternalMethodDispatch *, OSObject *, void *) {
    if (!arguments || selector >= static_cast<uint32_t>(n48native::ShellSelector::Count))
        return kIOReturnUnsupported;
    auto *service = OSDynamicCast(Navi48Native, getProvider());
    if (!service) return kIOReturnNotReady;
    uint32_t outCount = arguments->scalarOutputCount;
    const IOReturn result = service->shellDispatch(selector, arguments->scalarInput, arguments->scalarInputCount,
                                                  arguments->scalarOutput, outCount);
    arguments->scalarOutputCount = outCount;
    return result;
}
IOReturn Navi48Shell::shellAction(OSObject *, void *, void *, void *, void *) { return kIOReturnNotReady; }
IOReturn Navi48Shell::shellGated(uint32_t, const uint64_t *, uint32_t, uint64_t *, uint32_t &) { return kIOReturnNotReady; }
n48native::IOKitController *Navi48Shell::controller() const { return nullptr; }
n48native::ExperimentalCompute *Navi48Shell::compute() const { return nullptr; }
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
// Read-only shell backend: the lifecycle double never touches hardware, so
// every shell read is refused. The gate/refusal plumbing is covered by the
// service smoke test below; real reads are kext-build only.
IOReturn ExperimentalCompute::shellSnapshot(Snapshot &out) const { out = facts_; return kIOReturnSuccess; }
IOReturn ExperimentalCompute::shellReadGc(uint32_t, int, uint32_t &) const { return kIOReturnNotReady; }
IOReturn ExperimentalCompute::shellReadMmhub(uint32_t, uint32_t &) const { return kIOReturnNotReady; }
IOReturn ExperimentalCompute::shellReadBar0(uint64_t, uint32_t &) const { return kIOReturnNotReady; }
IOReturn ExperimentalCompute::checkNoAccelerator(bool &iteratorNull) {
    iteratorNull = false;
    return kIOReturnNotReady; // stub has no registry doubles; real function is host-tested separately
}
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
