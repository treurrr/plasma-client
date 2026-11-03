#version 450

layout(location = 0) in vec2 local;
layout(location = 1) in vec2 half_size;
layout(location = 2) in vec3 params;
layout(location = 3) in vec4 fill_top;
layout(location = 4) in vec4 fill_bottom;
layout(location = 5) in vec4 border_colour;
layout(location = 6) in vec4 glow_colour;

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

void main() {
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
