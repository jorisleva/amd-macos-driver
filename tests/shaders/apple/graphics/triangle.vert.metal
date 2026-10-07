#include <metal_stdlib>
using namespace metal;

// Shared byte layout with solid.frag.metal and OffscreenHarness::Draw:
// color at 0, shape at 16, padding at 20/24/28, size 32, alignment 16.
struct DrawParams { float4 color; uint shape; uint padding0; uint padding1; uint padding2; };
struct RasterPosition { float4 position [[position]]; };

vertex RasterPosition triangle_vertex(uint vertexID [[vertex_id]],
                                      constant DrawParams &draw [[buffer(0)]]) {
    float2 first = vertexID == 0 ? float2(-0.5f, -0.484375f)
                 : vertexID == 1 ? float2(0.5f, -0.484375f) : float2(-0.5f, 0.515625f);
    float2 second = vertexID == 0 ? float2(0.5f, -0.484375f)
                  : vertexID == 1 ? float2(0.5f, 0.515625f) : float2(-0.5f, -0.484375f);
    return {float4(draw.shape == 0 ? first : second, 0.0f, 1.0f)};
}
