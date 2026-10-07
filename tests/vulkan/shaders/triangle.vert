#version 450
layout(push_constant) uniform Draw { vec4 color; uint shape; } draw;
void main() {
    const vec2 first[3] = vec2[3](vec2(-0.5, -0.484375), vec2(0.5, -0.484375), vec2(-0.5, 0.515625));
    const vec2 second[3] = vec2[3](vec2(0.5, -0.484375), vec2(0.5, 0.515625), vec2(-0.5, -0.484375));
    gl_Position = vec4(draw.shape == 0 ? first[gl_VertexIndex] : second[gl_VertexIndex], 0, 1);
}
