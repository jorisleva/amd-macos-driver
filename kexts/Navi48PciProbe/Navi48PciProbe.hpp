#pragma once
#include <IOKit/IOService.h>

// Independent observer category: not a framebuffer, accelerator or native client.
class Navi48PciProbe final : public IOService {
    OSDeclareDefaultStructors(Navi48PciProbe)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **handler) override;
    IOReturn newUserClient(task_t, void *, UInt32, IOUserClient **handler) override;
};
