#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_colour;

layout(push_constant) uniform Screen {
    vec2 inverse_size;
    float linearise;
} screen;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 tint;

vec3 to_linear(vec3 colour) {
    return mix(colour / 12.92, pow((colour + 0.055) / 1.055, vec3(2.4)), step(0.04045, colour));
}

void main() {
    gl_Position = vec4(in_position * screen.inverse_size * 2.0 - 1.0, 0.0, 1.0);
    uv = in_uv;
    tint = vec4(float((in_colour >> 24) & 0xffu), float((in_colour >> 16) & 0xffu),
                float((in_colour >> 8) & 0xffu), float(in_colour & 0xffu)) / 255.0;
    if (screen.linearise > 0.5) tint.rgb = to_linear(tint.rgb);
}
