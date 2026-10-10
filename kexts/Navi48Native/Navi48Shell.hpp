// SPDX-License-Identifier: MIT
#pragma once
// Read-only interactive shell for the retained native service.
//
// Design contract (explicit user request, 2026-10-10):
// - READ-ONLY: register/snapshot reads only. No MMIO/VRAM write, no firmware,
//   no doorbell, no command submission, no power/clock change. Ever.
// - Only callable while the service holds GPU resources (post-hardware).
//   Before hardware, open() is refused: nothing to inspect.
// - All calls serialize through the service workloop gate. No concurrent
//   access, no reentrancy into the compute engine.
// - Selectors are stable IDs. Unknown selector => kIOReturnUnsupported.
// - Output is a fixed-size value struct: no pointers, no descriptors, no
//   kernel addresses leak beyond the already-published BAR mappings.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
// Forward declarations: the shell never touches these objects directly;
// all reads go through Navi48Native::shellDispatch under its gate.
namespace n48native { class IOKitController; class ExperimentalCompute; } // namespace n48native
namespace n48native {
// Shell selectors (stable).
enum class ShellSelector : uint32_t {
    Snapshot = 0, // full compute snapshot values (stage/result/lanes/fences)
    ReadGcReg = 1, // scalarInput[0] = GC dword offset (validated range)
    ReadMmhubReg = 2, // scalarInput[0] = MMHUB dword offset (validated range)
    ReadBar0Word = 3, // scalarInput[0] = BAR0 byte offset (scratch only)
    RingTestSurvey = 4, // re-read GC survey fields (no writes)
    Count = 5
};
// Fixed-size read-only reply. All values, no pointers.
struct ShellReply {
    uint64_t values[8]{};
    uint32_t result{0};
};
// GC dword-offset allowlist for ReadGcReg: survey registers only.
// SCRATCH_REG0 (0x2040/BIDX1), RB0_RPTR (0x0F60/BIDX0), ME_CNTL (0x0803/BIDX1),
// MEC_RS64_CNTL (0x2904/BIDX1). Absolute dword offsets resolved via the same
// SOC15 helper the engine uses; the shell validates the resolved address is
// within the adopted BAR mapping before reading.
struct ShellGcAllowlist {
    uint32_t reg{0};
    int baseIdx{0};
};
} // namespace n48native

// IOUserClient subclass, owned by the Navi48Native service. Created only via
// newUserClient with the explicit shell opt-in boot-arg; otherwise refused.
class Navi48Shell final : public IOUserClient {
    OSDeclareDefaultStructors(Navi48Shell)
public:
    bool initWithTask(task_t owningTask, void *securityToken, UInt32 type) override;
    bool start(IOService *provider) override;
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch = nullptr,
                            OSObject *target = nullptr, void *reference = nullptr) override;
private:
    static IOReturn shellAction(OSObject *, void *, void *, void *, void *);
    IOReturn shellGated(uint32_t selector, const uint64_t *scalarInput, uint32_t scalarInputCount,
                        uint64_t *scalarOutput, uint32_t &scalarOutputCount);
    n48native::IOKitController *controller() const;
    n48native::ExperimentalCompute *compute() const;
};
