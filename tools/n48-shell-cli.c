// SPDX-License-Identifier: MIT
// Read-only userspace client for the Navi48Shell IOUserClient.
// Opens Navi48Native (triple opt-in service, post-hardware) and calls the
// five read-only selectors. No writes, no MMIO, no submission: the shell
// refuses everything except register/snapshot reads. Exit codes: 0 ok,
// 1 usage/connection error, 2 shell refused (pre-hardware or no opt-in).
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *kServiceClass = "Navi48Native";
static const uint32_t kType = 0;

static int callMethod(io_connect_t conn, uint32_t selector,
                      const uint64_t *in, uint32_t inCount) {
    uint64_t out[8] = {0};
    uint32_t outCount = 8;
    kern_return_t kr = IOConnectCallScalarMethod(conn, selector, in, inCount, out, &outCount);
    if (kr != KERN_SUCCESS) {
        printf("selector %u: IOConnectCallScalarMethod failed 0x%x\n", selector, kr);
        return 1;
    }
    printf("selector %u: outCount=%u", selector, outCount);
    for (uint32_t i = 0; i < outCount; i++) printf(" out[%u]=0x%llx", i, (unsigned long long)out[i]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault,
                                                       IOServiceMatching(kServiceClass));
    if (!service) {
        printf("Navi48Native service not found (not loaded or retired)\n");
        return 1;
    }
    io_connect_t conn = 0;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), kType, &conn);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) {
        printf("IOServiceOpen failed 0x%x (shell needs triple opt-in + post-hardware)\n", kr);
        return 2;
    }
    int failures = 0;
    if (!strcmp(which, "all") || !strcmp(which, "snapshot")) {
        failures |= callMethod(conn, 0, NULL, 0);
    }
    if (!strcmp(which, "all") || !strcmp(which, "gc")) {
        const uint64_t regs[4] = {0x2040, 0x0F60, 0x0803, 0x2904};
        for (int i = 0; i < 4; i++) failures |= callMethod(conn, 1, &regs[i], 1);
    }
    if (!strcmp(which, "all") || !strcmp(which, "mmhub")) {
        const uint64_t regs[3] = {0x554, 0x555, 0x4c7};
        for (int i = 0; i < 3; i++) failures |= callMethod(conn, 2, &regs[i], 1);
    }
    if (!strcmp(which, "all") || !strcmp(which, "survey")) {
        failures |= callMethod(conn, 4, NULL, 0);
    }
    if (!strcmp(which, "all") || !strcmp(which, "bar0")) {
        const uint64_t scratch = 0x4000000; // BAR0-relative scratch base (read-only)
        failures |= callMethod(conn, 3, &scratch, 1);
    }
    IOServiceClose(conn);
    return failures ? 1 : 0;
}
