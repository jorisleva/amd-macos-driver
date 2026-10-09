// SPDX-License-Identifier: MIT
#pragma once

// Deliberately no old diagnostic user-client ABI, global capture buffer or
// Navi48Bringup/Apple perf callbacks. Not a silent no-op logger.
namespace amdgpu {
void n48_logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
}
