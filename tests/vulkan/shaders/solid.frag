#version 450
layout(push_constant) uniform Draw { vec4 color; uint shape; } draw;
layout(location = 0) out vec4 outputColor;
void main() { outputColor = draw.color; }
