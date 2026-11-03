#version 450

layout(location = 0) in vec4 in_rect;
layout(location = 1) in vec3 in_params;
layout(location = 2) in uint in_fill_top;
layout(location = 3) in uint in_fill_bottom;
layout(location = 4) in uint in_border;
layout(location = 5) in uint in_glow;

layout(push_constant) uniform Screen {
    vec2 inverse_size;
    float linearise;
} screen;

layout(location = 0) out vec2 local;
layout(location = 1) out vec2 half_size;
layout(location = 2) out vec3 params;
layout(location = 3) out vec4 fill_top;
layout(location = 4) out vec4 fill_bottom;
layout(location = 5) out vec4 border_colour;
layout(location = 6) out vec4 glow_colour;

vec3 to_linear(vec3 colour) {
    return mix(colour / 12.92, pow((colour + 0.055) / 1.055, vec3(2.4)), step(0.04045, colour));
}

vec4 unpack(uint rgba) {
    vec4 out_colour = vec4(float((rgba >> 24) & 0xffu), float((rgba >> 16) & 0xffu),
                           float((rgba >> 8) & 0xffu), float(rgba & 0xffu)) / 255.0;
    return vec4(screen.linearise > 0.5 ? to_linear(out_colour.rgb) : out_colour.rgb,
                out_colour.a);
}

void main() {
    const vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                                    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

    half_size = in_rect.zw * 0.5;
    params = in_params;
    fill_top = unpack(in_fill_top);
    fill_bottom = unpack(in_fill_bottom);
    border_colour = unpack(in_border);
    glow_colour = unpack(in_glow);

    // The quad is grown past the rectangle so the glow and the antialiased
    // edge have somewhere to land.
    vec2 grown = half_size + in_params.z + 1.5;
    local = (corners[gl_VertexIndex] * 2.0 - 1.0) * grown;
    gl_Position = vec4((in_rect.xy + half_size + local) * screen.inverse_size * 2.0 - 1.0,
                       0.0, 1.0);
}
