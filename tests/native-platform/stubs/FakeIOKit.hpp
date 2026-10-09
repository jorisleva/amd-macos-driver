// SPDX-License-Identifier: MIT
// Host-only IOKit doubles for the SAME resource-controller TU. Independent of
// the PCI observer's stubs. Mapping VAs are opaque/non-dereferenceable sentinels:
// an accidental BAR read/write will fail, not silently pass on simulated data.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

using UInt8 = uint8_t;
using UInt16 = uint16_t;
using UInt32 = uint32_t;
using IOByteCount = uint64_t;
using IOOptionBits = uint32_t;
using IOReturn = uint32_t;
using kern_return_t = uint32_t;
using task_t = void *;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnNotReady = 1;
constexpr IOReturn kIOReturnUnsupported = 2;
constexpr IOReturn kIOReturnTimeout = 3;
constexpr IOReturn kIOReturnIOError = 4;
constexpr IOReturn kIOReturnBadArgument = 5;
constexpr IOOptionBits kIOMapAnywhere = 0x1, kIOMapCacheMask = 0xf00, kIOMapInhibitCache = 0x100;
constexpr IOOptionBits kIOMapReadOnly = 0x1000, kIOMapUnique = 0x4000000, kIOMemoryMapperNone = 0x800;
inline int kernelTaskTag;
inline task_t kernel_task = &kernelTaskTag;
class IOPlatformExpert;
namespace fake {
inline unsigned liveObjects = 0, liveLocks = 0, mapsMade = 0, mapsFreed = 0, bootReads = 0;
inline bool bootPresent = true, lockFails = false;
inline uint32_t bootValue = 1;
inline IOPlatformExpert *platform = nullptr;
// Event >= 0: map released (bar index); 10: provider closed; 20+index:
// descriptor freed. Borrowed descriptor ownership is checked via refcounts too.
inline std::vector<unsigned> events;
}
class OSObject {
    unsigned refs_ = 1;
public:
    OSObject() { ++fake::liveObjects; }
    virtual ~OSObject() { --fake::liveObjects; }
    void retain() { ++refs_; }
    virtual void release() { if (!--refs_) delete this; }
    unsigned references() const { return refs_; }
    OSObject(const OSObject &) = delete;
    OSObject &operator=(const OSObject &) = delete;
};
struct IOLock { std::mutex mutex; };
inline IOLock *IOLockAlloc() {
    if (fake::lockFails) return nullptr;
    ++fake::liveLocks; return new IOLock;
}
inline void IOLockFree(IOLock *lock) { --fake::liveLocks; delete lock; }
inline void IOLockLock(IOLock *lock) { lock->mutex.lock(); }
inline void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }
void IOLog(const char *, ...);
void IOSleep(unsigned);

class IOService : public OSObject {
public:
    IOService *provider = nullptr;
    bool inactive = false;
    bool isInactive() const { return inactive; }
    virtual IOService *getProvider() const { return provider; }
    static IOPlatformExpert *getPlatform() { return fake::platform; }
};
class IOMemoryDescriptor;
struct MapRecipe {
    bool fail = false;
    uint64_t address = 0, physical = 0, bytes = 0, contiguous = 0;
    IOOptionBits options = 0;
    task_t task = kernel_task;
    IOMemoryDescriptor *backing = nullptr;
};
class IOMemoryMap : public OSObject {
    IOMemoryDescriptor *retained_;
    unsigned index_;
public:
    MapRecipe recipe;
    IOMemoryMap(IOMemoryDescriptor *descriptor, unsigned index, MapRecipe input);
    ~IOMemoryMap() override;
    uint64_t getAddress() { return recipe.address; }
    virtual IOByteCount getLength() { return recipe.bytes; }
    virtual uint64_t getPhysicalSegment(IOByteCount, IOByteCount *length, IOOptionBits options = 0) {
        if (options != kIOMemoryMapperNone) return 0; // no CPU=DMA substitution
        *length = recipe.contiguous; return recipe.physical;
    }
    virtual IOOptionBits getMapOptions() { return recipe.options; }
    virtual task_t getAddressTask() { return recipe.task; }
    virtual IOMemoryDescriptor *getMemoryDescriptor() { return recipe.backing; }
    // Deliberately no unmap/redirect API: a shared map must never be forced away.
};
class IOMemoryDescriptor : public OSObject {
public:
    unsigned index = 0, mapCalls = 0;
    uint64_t bytes = 0, physical = 0, contiguous = 0;
    IOOptionBits lastSegmentOptions = 0, lastMapOptions = 0;
    MapRecipe recipe;
    IOMemoryMap *lastMap = nullptr; // borrowed, valid only while controller holds it
    void (*afterMap)(IOMemoryDescriptor *) = nullptr;
    ~IOMemoryDescriptor() override { fake::events.push_back(20 + index); }
    virtual IOByteCount getLength() const { return bytes; }
    virtual uint64_t getPhysicalSegment(IOByteCount offset, IOByteCount *length, IOOptionBits options = 0) {
        lastSegmentOptions = options;
        if (offset || options != kIOMemoryMapperNone) return 0;
        *length = contiguous; return physical;
    }
    virtual IOMemoryMap *map(IOOptionBits options = 0) {
        ++mapCalls; lastMapOptions = options;
        if (recipe.fail) return nullptr;
        lastMap = new IOMemoryMap(this, index, recipe);
        if (afterMap) afterMap(this);
        return lastMap;
    }
};
using IODeviceMemory = IOMemoryDescriptor;
inline IOMemoryMap::IOMemoryMap(IOMemoryDescriptor *descriptor, unsigned index, MapRecipe input)
    : retained_(descriptor), index_(index), recipe(input) { retained_->retain(); ++fake::mapsMade; }
inline IOMemoryMap::~IOMemoryMap() {
    fake::events.push_back(index_); ++fake::mapsFreed;
    retained_->release();
}
struct PE_Video {
    unsigned long v_baseAddr = 0, v_rowBytes = 0, v_width = 0, v_height = 0, v_depth = 0, v_display = 0;
    char v_pixelFormat[64]{};
    unsigned long v_offset = 0, v_length = 0;
};
class IOPlatformExpert : public IOService {
public:
    PE_Video video;
    IOReturn status = kIOReturnSuccess;
    unsigned calls = 0;
    void (*afterConsole)(IOPlatformExpert *) = nullptr;
    virtual IOReturn getConsoleInfo(PE_Video *out) {
        ++calls; *out = video;
        if (afterConsole) afterConsole(this);
        return status;
    }
};
class IOPCIDevice : public IOService {
public:
    uint8_t config[256]{};
    IODeviceMemory *descriptors[3]{};
    IOService *client = nullptr;
    unsigned opens = 0, closes = 0, configReads = 0, descriptorReads = 0;
    bool openSucceeds = true;
    uint8_t bus = 7, device = 0, function = 0;
    void (*afterRead)(IOPCIDevice *, IOByteCount) = nullptr;
    void (*afterClose)(IOPCIDevice *) = nullptr;
    void set32(unsigned offset, uint32_t value) { std::memcpy(config + offset, &value, sizeof(value)); }
    void set16(unsigned offset, uint16_t value) { std::memcpy(config + offset, &value, sizeof(value)); }
    // Match the public SDK wrappers, which call the extended config API.
    uint32_t configRead32(IOByteCount offset) { return extendedConfigRead32(offset); }
    uint16_t configRead16(IOByteCount offset) { return extendedConfigRead16(offset); }
    uint8_t configRead8(IOByteCount offset) { return extendedConfigRead8(offset); }
    uint32_t extendedConfigRead32(IOByteCount offset) {
        ++configReads; uint32_t result; std::memcpy(&result, config + offset, sizeof(result));
        if (afterRead) afterRead(this, offset);
        return result;
    }
    uint16_t extendedConfigRead16(IOByteCount offset) {
        ++configReads; uint16_t result; std::memcpy(&result, config + offset, sizeof(result));
        if (afterRead) afterRead(this, offset);
        return result;
    }
    uint8_t extendedConfigRead8(IOByteCount offset) {
        ++configReads; const uint8_t result = config[offset];
        if (afterRead) afterRead(this, offset);
        return result;
    }
    virtual bool isOpen(const IOService *forClient = nullptr) const { return client && (!forClient || client == forClient); }
    virtual bool open(IOService *forClient, IOOptionBits options = 0, void *arg = nullptr) {
        ++opens;
        if (options || arg || client || !openSucceeds) return false;
        client = forClient; return true;
    }
    virtual void close(IOService *forClient, IOOptionBits options = 0) {
        if (options || client != forClient) return;
        ++closes; fake::events.push_back(10); client = nullptr;
        if (afterClose) afterClose(this);
    }
    virtual uint8_t getBusNumber() { return bus; }
    virtual uint8_t getDeviceNumber() { return device; }
    virtual uint8_t getFunctionNumber() { return function; }
    virtual IODeviceMemory *getDeviceMemoryWithRegister(uint8_t reg) {
        ++descriptorReads;
        if (reg == 0x10) return descriptors[0];
        if (reg == 0x18) return descriptors[1];
        if (reg == 0x24) return descriptors[2];
        return nullptr;
    }
    // No writes, seize, interrupts, DMA, registration or power-management API.
};
inline bool PE_parse_boot_argn(const char *key, void *out, int bytes) {
    ++fake::bootReads;
    if (std::strcmp(key, "navi48-native-platform") || bytes != 4 || !fake::bootPresent) return false;
    std::memcpy(out, &fake::bootValue, 4); return true;
}
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
