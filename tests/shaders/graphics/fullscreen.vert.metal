#include <metal_stdlib>
using namespace metal;

struct RasterPosition { float4 position [[position]]; };

// Equivalent to fullscreen.vert: no vertex buffers, one oversized triangle.
vertex RasterPosition fullscreen_vertex(uint vertexID [[vertex_id]]) {
    float2 p = vertexID == 0 ? float2(-1.0f, -1.0f)
             : vertexID == 1 ? float2(3.0f, -1.0f) : float2(-1.0f, 3.0f);
    return {float4(p, 0.0f, 1.0f)};
}
