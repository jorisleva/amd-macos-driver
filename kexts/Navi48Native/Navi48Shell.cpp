// SPDX-License-Identifier: MIT
// Read-only interactive shell for the retained native service.
//
// Contract (explicit user request, 2026-10-10):
// - READ-ONLY: register/snapshot reads only. No MMIO/VRAM write, no firmware,
//   no doorbell, no submission, no power/clock change. Ever.
// - Only callable while the service holds GPU resources (post-hardware).
// - All reads serialize through the service workloop gate via
//   Navi48Native::shellDispatch. The shell holds no engine pointers.
// - Selectors are stable IDs. Unknown selector => kIOReturnUnsupported.
// - Triple opt-in: compute=1 + risk=1 + shell=1 boot-args, else newUserClient
//   is refused exactly like before (kIOReturnUnsupported).
#include "Navi48Shell.hpp"
#include "Navi48Native.hpp"

OSDefineMetaClassAndStructors(Navi48Shell, IOUserClient)

bool Navi48Shell::initWithTask(task_t owningTask, void *securityToken, UInt32 type) {
    if (type != 0) return false; // type 0 is the only shell
    return IOUserClient::initWithTask(owningTask, securityToken, type);
}
bool Navi48Shell::start(IOService *provider) {
    auto *service = OSDynamicCast(Navi48Native, provider);
    if (!service || !service->shellAvailable()) return false;
    return IOUserClient::start(provider);
}
IOReturn Navi48Shell::clientClose() {
    return kIOReturnSuccess; // no per-client state; the service retains everything
}
IOReturn Navi48Shell::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                    IOExternalMethodDispatch *, OSObject *, void *) {
    if (!arguments || arguments->version != kIOExternalMethodArgumentsCurrentVersion) return kIOReturnBadArgument;
    if (selector >= static_cast<uint32_t>(n48native::ShellSelector::Count)) return kIOReturnUnsupported;
    if (!arguments->scalarOutput || arguments->scalarOutputCount < 8) return kIOReturnBadArgument;
    auto *service = OSDynamicCast(Navi48Native, getProvider());
    if (!service) return kIOReturnNotReady;
    uint32_t outCount = arguments->scalarOutputCount;
    const IOReturn result = service->shellDispatch(selector, arguments->scalarInput, arguments->scalarInputCount,
                                                  arguments->scalarOutput, outCount);
    arguments->scalarOutputCount = outCount;
    return result;
}
IOReturn Navi48Shell::shellAction(OSObject *, void *, void *, void *, void *) {
    return kIOReturnNotReady; // unused; dispatch goes through Navi48Native::shellDispatch
}
IOReturn Navi48Shell::shellGated(uint32_t, const uint64_t *, uint32_t, uint64_t *, uint32_t &) {
    return kIOReturnNotReady; // unused; dispatch goes through Navi48Native::shellDispatch
}
n48native::IOKitController *Navi48Shell::controller() const { return nullptr; }
n48native::ExperimentalCompute *Navi48Shell::compute() const { return nullptr; }
