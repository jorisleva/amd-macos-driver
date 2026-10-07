#include <metal_stdlib>
using namespace metal;

// Same ABI as the Vulkan control: four storage buffers, set 0 / bindings 0..3.
// Dispatch complete groups of 64; this explicit bound protects surplus threads.
struct Params { uint count; uint offset; };
kernel void vector_add(device const uint *a [[buffer(0)]],
                       device const uint *b [[buffer(1)]],
                       device uint *result [[buffer(2)]],
                       device const Params *params [[buffer(3)]],
                       uint i [[thread_position_in_grid]]) {
    if (i < params->count) {
        uint j = i + params->offset;
        result[j] = a[j] + b[j];
    }
}
