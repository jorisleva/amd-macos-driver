#include <metal_stdlib>
using namespace metal;

// Same 32-byte DrawParams layout as triangle.vert.metal.
struct DrawParams { float4 color; uint shape; uint padding0; uint padding1; uint padding2; };

// Fixed-function blending, not this shader, combines the two ordered draws.
fragment float4 solid_fragment(constant DrawParams &draw [[buffer(0)]]) {
    return draw.color;
}
