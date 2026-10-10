// SPDX-License-Identifier: MIT
// Host test for the extracted accelerator-exclusion preflight.
// The full ExperimentalCompute::run() needs the SDK + engines (kext-build
// only); checkNoAccelerator() needs only IOService matching doubles.
#pragma once
#include <IOKit/IOService.h>
// Host doubles define a small sequential IOReturn set; the SDK uses sparse
// iokit_common_err codes. The preflight only compares equality, so map the
// double values to the SDK ones when compiling against the real headers.
#ifndef kIOReturnExclusiveAccess
#define kIOReturnExclusiveAccess 8
#endif
#ifndef kIOReturnNoMemory
#define kIOReturnNoMemory 6
#endif

namespace n48native {
// Returns kIOReturnSuccess when no competitor exists: a non-null iterator
// that yields nothing, or a null iterator (observed on real Tahoe with no
// accelerator: null = presumed empty set, recorded in iteratorNull).
// Any object found is kIOReturnExclusiveAccess. A null matching dictionary
// is kIOReturnNoMemory. No GPU, firmware, mapping or state change.
inline IOReturn checkNoAccelerator(bool &iteratorNull) {
    iteratorNull = false;
    auto *matching = IOService::serviceMatching("IOAccelerator");
    if (!matching) return kIOReturnNoMemory;
    auto *accelerators = IOService::getMatchingServices(matching);
    matching->release();
    if (!accelerators) { iteratorNull = true; return kIOReturnSuccess; }
    const bool anotherGpu = accelerators->getNextObject() != nullptr;
    accelerators->release();
    return anotherGpu ? kIOReturnExclusiveAccess : kIOReturnSuccess;
}
} // namespace n48native
