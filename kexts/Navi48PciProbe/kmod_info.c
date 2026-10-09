#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>

extern kern_return_t _start(kmod_info_t *info, void *data);
extern kern_return_t _stop(kmod_info_t *info, void *data);
static kern_return_t probe_start(kmod_info_t *info, void *data) {
    (void)info; (void)data; return KERN_SUCCESS;
}
static kern_return_t probe_stop(kmod_info_t *info, void *data) {
    (void)info; (void)data; return KERN_SUCCESS;
}
KMOD_EXPLICIT_DECL(com.amd-macos-driver.Navi48PciProbe, "0.1.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = probe_start;
__private_extern__ kmod_stop_func_t *_antimain = probe_stop;
