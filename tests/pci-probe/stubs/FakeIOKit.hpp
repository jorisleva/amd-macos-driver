// Narrow host-only IOKit double. Deliberately NO PCI config, mapping, open,
// interrupts, DMA, power-management or service-registration APIs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using SInt32 = int32_t;
using UInt32 = uint32_t;
using IOReturn = uint32_t;
using task_t = void *;
constexpr IOReturn kIOReturnUnsupported = 0xe00002c7u;
class IOUserClient;

namespace fake {
inline int live = 0;
inline int budget = -1; // successful allocation/insertion operations until failure
inline bool bootPresent = false;
inline uint32_t bootValue = 0;
inline bool baseStartSucceeds = true;
inline bool baseProbeSucceeds = true;
inline unsigned logs = 0;
inline bool admit() {
    if (budget < 0) return true;
    if (budget == 0) return false;
    --budget;
    return true;
}
} // namespace fake

class OSObject {
    unsigned refs_ = 1;
public:
    OSObject() { ++fake::live; }
    virtual ~OSObject() { --fake::live; }
    void retain() { ++refs_; }
    void release() { if (--refs_ == 0) delete this; }
    unsigned references() const { return refs_; }
    OSObject(const OSObject &) = delete;
    OSObject &operator=(const OSObject &) = delete;
};
class OSData : public OSObject {
    std::vector<uint8_t> bytes_;
public:
    static OSData *withBytes(const void *data, unsigned length) {
        if (!fake::admit()) return nullptr;
        auto *result = new OSData;
        if (length) {
            const auto *p = static_cast<const uint8_t *>(data);
            result->bytes_.assign(p, p + length);
        }
        return result;
    }
    const void *getBytesNoCopy() const { return bytes_.data(); }
    unsigned getLength() const { return static_cast<unsigned>(bytes_.size()); }
};
class OSNumber : public OSObject {
    uint64_t value_;
    explicit OSNumber(uint64_t value) : value_(value) {}
public:
    static OSNumber *withNumber(uint64_t value, unsigned) {
        return fake::admit() ? new OSNumber(value) : nullptr;
    }
    uint64_t unsigned64BitValue() const { return value_; }
};
class OSDictionary : public OSObject {
    std::map<std::string, OSObject *> entries_;
public:
    static OSDictionary *withCapacity(unsigned) {
        return fake::admit() ? new OSDictionary : nullptr;
    }
    ~OSDictionary() override { for (auto &entry : entries_) entry.second->release(); }
    bool setObject(const char *key, OSObject *object) {
        if (!object || !fake::admit()) return false;
        object->retain();
        auto &old = entries_[key];
        if (old) old->release();
        old = object;
        return true;
    }
    OSObject *getObject(const char *key) const {
        auto pos = entries_.find(key);
        return pos == entries_.end() ? nullptr : pos->second;
    }
    void removeObject(const char *key) {
        auto pos = entries_.find(key);
        if (pos != entries_.end()) { pos->second->release(); entries_.erase(pos); }
    }
    size_t count() const { return entries_.size(); }
};
class IOService : public OSObject {
    OSDictionary properties_;
public:
    mutable unsigned reads = 0;
    unsigned writes = 0, probes = 0, starts = 0, stops = 0;
    virtual IOService *probe(IOService *, SInt32 *) {
        ++probes;
        return fake::baseProbeSucceeds ? this : nullptr;
    }
    virtual bool start(IOService *) { ++starts; return fake::baseStartSucceeds; }
    virtual void stop(IOService *) { ++stops; }
    virtual IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) {
        return kIOReturnUnsupported;
    }
    virtual IOReturn newUserClient(task_t, void *, UInt32, IOUserClient **) {
        return kIOReturnUnsupported;
    }
    OSObject *copyProperty(const char *key) const {
        ++reads;
        OSObject *p = properties_.getObject(key);
        if (p) p->retain();
        return p;
    }
    bool setProperty(const char *key, OSObject *object) {
        ++writes;
        return properties_.setObject(key, object);
    }
    void removeProperty(const char *key) { ++writes; properties_.removeObject(key); }
    OSObject *getProperty(const char *key) const { return properties_.getObject(key); }
    size_t propertyCount() const { return properties_.count(); }
};
class IOPCIDevice : public IOService {};
inline bool PE_parse_boot_argn(const char *key, void *value, int length) {
    if (std::strcmp(key, "navi48-pci-probe") || length != 4 || !fake::bootPresent) return false;
    std::memcpy(value, &fake::bootValue, 4);
    return true;
}
inline void IOLog(const char *, ...) { ++fake::logs; }

#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
#define OSDeclareDefaultStructors(type) public: type() = default; ~type() override = default;
#define OSDefineMetaClassAndStructors(type, base)
