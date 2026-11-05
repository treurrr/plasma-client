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
layout(location = 10) flat in vec4 extra;

// Inventory icons. Every item the server knows is one layer, so an item id
// needs no lookup at all.
layout(set = 0, binding = 0) uniform sampler2DArray items;

// The game's own interface sprites: the hotbar, the hearts, the crosshair.
layout(set = 0, binding = 1) uniform sampler2D gui;

// Pictures that arrive at run time, one per layer: server icons, skins.
layout(set = 0, binding = 2) uniform sampler2DArray dynamic_layers;

// The title screen's six panorama faces, as the cube's layers.
layout(set = 0, binding = 3) uniform sampler2DArray panorama_faces;

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

bool outside() {
    return any(greaterThan(abs(local), half_size));
}

// A direction to a layer and a texel of the cube, by the face table every
// cube map sampler follows. The layers are the game's: +x, -x, +y, -y, +z, -z.
vec3 cube_texel(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) {
        return d.x > 0.0 ? vec3(-d.z, -d.y, 0.0) / vec3(a.x, a.x, 1.0)
                         : vec3(d.z, -d.y, 1.0) / vec3(a.x, a.x, 1.0);
    }
    if (a.y >= a.z) {
        return d.y > 0.0 ? vec3(d.x, d.z, 2.0) / vec3(a.y, a.y, 1.0)
                         : vec3(d.x, -d.z, 3.0) / vec3(a.y, a.y, 1.0);
    }
    return d.z > 0.0 ? vec3(d.x, -d.y, 4.0) / vec3(a.z, a.z, 1.0)
                     : vec3(-d.x, -d.y, 5.0) / vec3(a.z, a.z, 1.0);
}

// The game draws the cube around a camera at its centre, the model view
// turned half a turn about x, then `pitch` about x, then `yaw` about y; this
// is that, inverted, for the ray through one pixel.
vec3 panorama_at(vec2 ndc, float yaw, float pitch, float aspect) {
    const float tan_half = tan(radians(85.0) * 0.5);
    vec3 d = vec3(ndc.x * tan_half * aspect, -ndc.y * tan_half, -1.0);
    d = vec3(d.x, -d.y, -d.z);
    float p = -radians(pitch);
    d = vec3(d.x, d.y * cos(p) - d.z * sin(p), d.y * sin(p) + d.z * cos(p));
    float y = -radians(yaw);
    d = vec3(d.x * cos(y) + d.z * sin(y), d.y, -d.x * sin(y) + d.z * cos(y));
    vec3 face = cube_texel(d);
    // The game flips every face upside down on its way into the cube.
    return texture(panorama_faces, vec3(face.x * 0.5 + 0.5, 0.5 - face.y * 0.5, face.z)).rgb;
}

void main() {
    if (layer == -2) {
        if (outside()) discard;
        vec4 texel = texture(gui, sprite_uv);
        if (texel.a < 0.01) discard;
        vec3 rgb = linearise > 0.5 ? to_linear(texel.rgb) : texel.rgb;
        colour = vec4(rgb * fill_top.rgb, texel.a * fill_top.a);
        return;
    }
    if (layer == -3) {
        if (outside()) discard;
        vec2 ndc = local / half_size;
        vec3 rgb = panorama_at(ndc, extra.x, extra.y, half_size.x / half_size.y);
        rgb = linearise > 0.5 ? to_linear(rgb) : rgb;
        colour = vec4(rgb * fill_top.rgb, fill_top.a);
        return;
    }
    if (layer <= -16) {
        if (outside()) discard;
        vec2 uv = local / half_size * 0.5 + 0.5;
        vec4 texel = texture(dynamic_layers, vec3(uv, float(-16 - layer)));
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
