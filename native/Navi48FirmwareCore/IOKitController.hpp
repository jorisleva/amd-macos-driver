// SPDX-License-Identifier: MIT
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "AmdGpuAccess.hpp"
#include "Preflight.hpp"

namespace n48native {
// This resource controller is built in a SEPARATE non-loadable archive. A future
// attached IOService must own it and route stop/termination/power events to it.
// No IOService personality, entry point, firmware invocation or GPU access here.
enum class PlatformPhase { Empty, MappedUnqualified, Failed, Released };
enum class PlatformResult {
    MappingsHeldUnqualified, Disabled, NoLock, Used, InvalidOwner, WrongProvider,
    ProviderInactive, ProviderBusy, OpenFailed, OpenLost, WrongCard,
    InvalidConfig, InvalidBar, MissingDescriptor, InvalidDescriptor,
    PhysicalOverlap, InvalidCandidate, ConsoleOverlap, MapFailed, InvalidMap,
    VirtualOverlap, ConfigurationChanged, ConsoleChanged, NotMapped
};
enum class ConsoleResult { Unavailable, InvalidGeometry, AmbiguousBase, OutsideBar0, LocatedInBar0 };
// Fine-grained BAR mapping check. Stable IDs for BootDiagnostics decoding.
// Does NOT change the fail-closed decision: any non-Ok still returns InvalidMap
// (or MapFailed/VirtualOverlap as before). Values only, no pointers retained.
enum class MapCheck : uint32_t {
    NotChecked = 0, Ok = 1, NullMap = 2, DescriptorMismatch = 3, TaskMismatch = 4,
    LengthMismatch = 5, ContiguousMismatch = 6, PhysicalMismatch = 7, CacheMismatch = 8,
    FlagsMismatch = 9, NullAddress = 10, UnalignedAddress = 11, RangeOverflow = 12,
    AddressChanged = 13
};
enum PlatformBlocker : uint32_t {
    MappingLease = 1u << 0, ConsolePlacement = 1u << 1,
    ExclusiveGpuOwnership = 1u << 2, VramGeometry = 1u << 3,
    VramReservation = 1u << 4, HdpCoherency = 1u << 5,
    DmaMapping = 1u << 6, RecoveryTest = 1u << 7, GpuQuiescence = 1u << 8
};
constexpr uint32_t kUnimplementedHardwareProofs = ExclusiveGpuOwnership | VramGeometry |
    VramReservation | HdpCoherency | DmaMapping | RecoveryTest | GpuQuiescence;
struct PlatformRequest {
    Range candidate; // BAR0-relative request, NOT a VRAM reservation or MC address
    uint64_t requiredBytes;
};
enum class DescriptorOrigin : uint32_t {
    Retained = 0, // mapping declares the descriptor we retained: strict path
    DeclaredAdopted = 1, // shared mapping path: mapping declares another object,
    // adopted ONLY after full revalidation (length/segment/provider/config)
    NotAdopted = 2 // declared object failed revalidation: still InvalidMap
};
struct BarObservation {
    uint8_t configRegister;
    uint32_t configLow, configHigh;
    uint64_t cpuPhysical, bytes;
    IOOptionBits reportedMapOptions;
    DescriptorOrigin descriptorOrigin{DescriptorOrigin::Retained};
    MapCheck mapCheck{MapCheck::NotChecked}; // observed in checkMaps(); NotChecked when never reached
    uint64_t observedAddress{0}; // virtual address returned by the map (0 when none)
    IOOptionBits observedMapOptions{0};
    uint64_t observedLength{0}, observedContiguous{0}, observedPhysical{0};
    bool observedDescriptorMatch{false}, observedTaskMatch{false};
    // Object-identity comparison, values only (no pointer retained/published).
    // Reread is a fresh provider lookup at checkMaps() time: distinguishes a
    // provider substitution (declared==reread!=retained) from a shared mapping
    // declaring another object (reread==retained!=declared).
    bool observedRereadPresent{false}, observedRereadMatch{false}, observedDeclaredIsReread{false};
};
struct ConsoleObservation {
    ConsoleResult result{ConsoleResult::Unavailable};
    uint64_t rawBase{0}, rowBytes{0}, width{0}, height{0}, depth{0}, offset{0}, length{0};
    Range allocationInBar0{}; // protects the ENTIRE console allocation, not only visible pixels
};
struct PlatformSnapshot {
    PlatformPhase phase{PlatformPhase::Empty};
    PlatformResult result{PlatformResult::NotMapped};
    uint32_t identity[6]{};
    uint32_t bdf{0};
    uint16_t command{0};
    uint8_t headerType{0}, failedBar{0};
    BarObservation bars[3]{}; // BAR0, BAR2, BAR5; BAR4/ROM are never mapped
    ConsoleObservation console{};
    Range candidateInBar0{}, candidateCpuPhysical{};
    bool providerOpen{false}, mappingsHeld{false}, observationsValid{false};
    bool accessEnabled{false};
    uint32_t blockers{MappingLease | ConsolePlacement | kUnimplementedHardwareProofs};
};

class IOKitController final {
public:
    IOKitController();
    ~IOKitController(); // caller must join users before destruction
    IOKitController(const IOKitController &) = delete;
    IOKitController &operator=(const IOKitController &) = delete;
    // One attempt per object, opt-in via navi48-native-platform=1. owner must be
    // attached to provider and alive. No seize, PCI writes, BAR sizing writes,
    // memory/bus-master enable, MMIO read/write or firmware invocation.
    PlatformResult acquire(IOService *owner, IOService *provider, const PlatformRequest &request);
    PlatformResult revalidate(); // uncertainty is terminal; releases this unpublished lease
    // Same checks, but on failure leaves invalid mappings/PCI lease retained so
    // the service can retire DMA OUTSIDE its gate BEFORE release()/PCI close.
    // Failure is terminal; observations/access are invalid, no retry/rearm.
    PlatformResult revalidateHeld();
    PlatformSnapshot snapshot(); // values only: NEVER returns pointers, maps or a DeviceContext
    void release(); // only unpublished resources exist in this implementation

private:
    struct Window {
        IODeviceMemory *descriptor{nullptr}; // our extra retain, in addition to provider/map retains
        // Way 1 adoption: the mapping declared another object that fully
        // revalidated. descriptor is then the ADOPTED object; retained is the
        // originally retained provider object (still returned by the stable
        // provider). Both retained; released together. Null when not adopted.
        IODeviceMemory *retained{nullptr};
        IOMemoryMap *map{nullptr}; // returned +1; released, never forcibly unmapped
        uint64_t address{0};
    } windows_[3];
    struct RetiredLease {
        Window windows[3]{};
        IOService *owner{nullptr};
        IOPCIDevice *pci{nullptr};
        bool providerOpen{false};
        ~RetiredLease(); // invoke foreign IOKit release/close only AFTER unlocking
    };
    IOLock *lock_{nullptr};
    IOService *owner_{nullptr};
    IOPCIDevice *pci_{nullptr};
    PlatformSnapshot facts_{};
    amdgpu::DeviceContext context_{}; // real VAs, PRIVATE, DISABLED, lifetime tied to windows_

    PlatformResult captureConfig(PlatformSnapshot &out);
    PlatformResult checkDescriptors();
    PlatformResult adoptDeclared(unsigned bar); // way 1: full revalidation, no state change on failure
    PlatformResult checkMaps();
    ConsoleObservation captureConsole();
    PlatformResult revalidateLocked();
    PlatformResult failLocked(PlatformResult result, RetiredLease &retired);
    void releaseLocked(RetiredLease &retired);
    void updateValidity();
};
} // namespace n48native
