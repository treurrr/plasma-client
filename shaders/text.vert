#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_colour;

layout(push_constant) uniform Screen {
    vec2 inverse_size;
} screen;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 tint;

void main() {
    gl_Position = vec4(in_position * screen.inverse_size * 2.0 - 1.0, 0.0, 1.0);
    uv = in_uv;
    tint = vec4(float((in_colour >> 24) & 0xffu), float((in_colour >> 16) & 0xffu),
                float((in_colour >> 8) & 0xffu), float(in_colour & 0xffu)) / 255.0;
}
