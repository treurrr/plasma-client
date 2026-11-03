#version 450

layout(set = 0, binding = 0) uniform sampler2DArray celestials;
layout(set = 0, binding = 1) uniform sampler2DArray end_sky;

layout(push_constant) uniform Sky {
    mat4 transform;
    vec4 colour;
    vec4 fog_colour;
    vec4 params;
} sky;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in float in_spherical;
layout(location = 3) in float in_cylindrical;
layout(location = 0) out vec4 out_colour;

float linear_fog(float distance, float start, float end) {
    if (distance <= start) return 0.0;
    if (distance >= end) return 1.0;
    return (distance - start) / (end - start);
}

void main() {
    int mode = int(sky.params.x);
    if (mode == 0) {
        float end = sky.params.z;
        float fog = max(linear_fog(in_spherical, 0.0, end), linear_fog(in_cylindrical, end, end));
        out_colour = vec4(mix(sky.colour.rgb, sky.fog_colour.rgb, fog * sky.fog_colour.a),
                          sky.colour.a);
    } else if (mode == 1) {
        out_colour = in_colour * sky.colour;
    } else if (mode == 2) {
        out_colour = sky.colour;
    } else if (mode == 3) {
        vec4 texel = texture(celestials, vec3(in_uv, sky.params.y));
        if (texel.a == 0.0) discard;
        out_colour = texel * sky.colour;
    } else {
        vec4 texel = texture(end_sky, vec3(in_uv, 0.0)) * in_colour;
        if (texel.a == 0.0) discard;
        out_colour = texel * sky.colour;
    }
}
