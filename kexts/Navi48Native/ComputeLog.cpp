// SPDX-License-Identifier: MIT
#include <IOKit/IOLib.h>
#include <libkern/libkern.h>
#include <stdarg.h>
#include "NativeLog.hpp"
namespace n48compute {
void n48_logf(const char *format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    IOLog("%s", buffer);
}
}
