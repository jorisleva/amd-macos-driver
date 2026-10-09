#pragma once
// Same numeric common IOKit errors; no framework or hardware implementation.
using kern_return_t = int;
constexpr kern_return_t kIOReturnSuccess = 0;
constexpr kern_return_t kIOReturnError = static_cast<int>(0xe00002bcu);
constexpr kern_return_t kIOReturnNoMemory = static_cast<int>(0xe00002bdu);
constexpr kern_return_t kIOReturnBadArgument = static_cast<int>(0xe00002c2u);
constexpr kern_return_t kIOReturnUnsupported = static_cast<int>(0xe00002c7u);
constexpr kern_return_t kIOReturnInternalError = static_cast<int>(0xe00002c9u);
constexpr kern_return_t kIOReturnIOError = static_cast<int>(0xe00002cau);
constexpr kern_return_t kIOReturnNotAligned = static_cast<int>(0xe00002d0u);
constexpr kern_return_t kIOReturnBusy = static_cast<int>(0xe00002d5u);
constexpr kern_return_t kIOReturnTimeout = static_cast<int>(0xe00002d6u);
constexpr kern_return_t kIOReturnNotReady = static_cast<int>(0xe00002d8u);
constexpr kern_return_t kIOReturnNoSpace = static_cast<int>(0xe00002dbu);
constexpr kern_return_t kIOReturnInvalid = static_cast<int>(0xe0000001u);
