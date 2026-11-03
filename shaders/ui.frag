#version 450

layout(location = 0) in vec2 local;
layout(location = 1) in vec2 half_size;
layout(location = 2) in vec3 params;
layout(location = 3) in vec4 fill_top;
layout(location = 4) in vec4 fill_bottom;
layout(location = 5) in vec4 border_colour;
layout(location = 6) in vec4 glow_colour;
layout(location = 7) flat in int layer;
layout(location = 8) flat in float linearise;
layout(location = 9) in vec2 sprite_uv;

// Inventory icons. Every item the server knows is one layer, so an item id
// needs no lookup at all.
layout(set = 0, binding = 0) uniform sampler2DArray items;

// The game's own interface sprites: the hotbar, the hearts, the crosshair.
layout(set = 0, binding = 1) uniform sampler2D gui;

layout(location = 0) out vec4 colour;

float rounded_box(vec2 point, vec2 extent, float radius) {
    vec2 q = abs(point) - extent + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

vec4 over(vec4 src, vec4 dst) {
    float a = src.a + dst.a * (1.0 - src.a);
    if (a <= 0.0) return vec4(0.0);
    return vec4((src.rgb * src.a + dst.rgb * dst.a * (1.0 - src.a)) / a, a);
}

vec3 to_linear(vec3 colour) {
    return mix(colour / 12.92, pow((colour + 0.055) / 1.055, vec3(2.4)), step(0.04045, colour));
}

void main() {
    if (layer == -2) {
        vec4 texel = texture(gui, sprite_uv);
        if (texel.a < 0.01) discard;
        vec3 rgb = linearise > 0.5 ? to_linear(texel.rgb) : texel.rgb;
        colour = vec4(rgb * fill_top.rgb, texel.a * fill_top.a);
        return;
    }
    if (layer >= 0) {
        vec2 uv = local / half_size * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) discard;
        vec4 texel = texture(items, vec3(uv, float(layer)));
        if (texel.a < 0.02) discard;
        vec3 rgb = linearise > 0.5 ? to_linear(texel.rgb) : texel.rgb;
        colour = vec4(rgb * fill_top.rgb, texel.a * fill_top.a);
        return;
    }

    float radius = min(params.x, min(half_size.x, half_size.y));
    float distance = rounded_box(local, half_size, radius);

    vec4 result = vec4(0.0);
    if (params.z > 0.0) {
        float falloff = 1.0 - smoothstep(0.0, params.z, distance);
        result = vec4(glow_colour.rgb, glow_colour.a * falloff * falloff);
    }

    float inside = 1.0 - smoothstep(-0.5, 0.5, distance);
    float down = clamp((local.y + half_size.y) / max(2.0 * half_size.y, 0.001), 0.0, 1.0);
    vec4 fill = mix(fill_top, fill_bottom, down);
    result = over(vec4(fill.rgb, fill.a * inside), result);

    if (params.y > 0.0) {
        float edge = inside * smoothstep(-params.y - 0.5, -params.y + 0.5, distance);
        result = over(vec4(border_colour.rgb, border_colour.a * edge), result);
    }

    if (result.a <= 0.002) discard;
    colour = result;
}
