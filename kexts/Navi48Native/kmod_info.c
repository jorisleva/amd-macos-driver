// SPDX-License-Identifier: MIT
#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>
#include <IOKit/IOLib.h>

extern kern_return_t _start(kmod_info_t *info, void *data);
extern kern_return_t _stop(kmod_info_t *info, void *data);
static kern_return_t native_start(kmod_info_t *info, void *data) {
    (void)info; (void)data;
    IOLog("Navi48Native 0.1.2: module loaded; PCI/DMA preparation opt-in only, GPU firmware blocked\n");
    return KERN_SUCCESS;
}
static kern_return_t native_stop(kmod_info_t *info, void *data) {
    (void)info; (void)data;
    // Module teardown is not a GPU quiesce operation. This version never exposes
    // resources to firmware; live IOService instances are subject to IOKit unload rules.
    return KERN_SUCCESS;
}
KMOD_EXPLICIT_DECL(com.amd-macos-driver.Navi48Native, "0.1.2", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = native_start;
__private_extern__ kmod_stop_func_t *_antimain = native_stop;
