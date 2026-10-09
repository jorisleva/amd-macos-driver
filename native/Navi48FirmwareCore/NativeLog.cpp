// SPDX-License-Identifier: MIT
#include "NativeLog.hpp"
#include <IOKit/IOLib.h>
#include <stdarg.h>

namespace amdgpu {
void n48_logf(const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (count < 0) {
        IOLog("Navi48FirmwareCore: log formatting failed\n");
        return;
    }
    if (static_cast<size_t>(count) >= sizeof(line)) {
        IOLog("%s [truncated]\n", line);
        return;
    }
    IOLog("%s", line);
}
} // namespace amdgpu
