#include <metal_stdlib>
using namespace metal;

// The Vulkan probe supplies a nearest, normalized, clamp-to-edge sampler and
// an RGBA8_UNORM 2D image with one mip. Preserve all four channels, including A.
fragment float4 texture_fragment(float4 position [[position]],
                                 texture2d<float, access::sample> sourceTexture [[texture(0)]],
                                 sampler sourceSampler [[sampler(0)]]) {
    float2 extent = float2(sourceTexture.get_width(), sourceTexture.get_height());
    float2 uv = position.xy / extent;
    return sourceTexture.sample(sourceSampler, uv, level(0.0f));
}
