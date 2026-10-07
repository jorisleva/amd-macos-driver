#version 450
layout(set = 0, binding = 0) uniform sampler2D sourceTexture;
layout(location = 0) out vec4 outputColor;
void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sourceTexture, 0));
    outputColor = texture(sourceTexture, uv);
}
