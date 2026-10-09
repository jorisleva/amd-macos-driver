#pragma once
#include <stdio.h>
#include <stdarg.h>
// No PCI, mapping, allocation, DMA or GPU API. Tests supply ordinary RAM;
// IOSleep drives an explicitly simulated response, never a hardware device.
void IOSleep(unsigned milliseconds);
void IOLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
int native_test_vsnprintf(char *buffer, size_t size, const char *format, va_list args);
#define vsnprintf native_test_vsnprintf
