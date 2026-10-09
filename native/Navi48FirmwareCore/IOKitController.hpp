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
struct BarObservation {
    uint8_t configRegister;
    uint32_t configLow, configHigh;
    uint64_t cpuPhysical, bytes;
    IOOptionBits reportedMapOptions;
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
    PlatformResult checkMaps();
    ConsoleObservation captureConsole();
    PlatformResult revalidateLocked();
    PlatformResult failLocked(PlatformResult result, RetiredLease &retired);
    void releaseLocked(RetiredLease &retired);
    void updateValidity();
};
} // namespace n48native
