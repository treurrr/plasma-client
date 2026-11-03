#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 fog;
    vec4 params;
    uvec4 bases;
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_colour;
layout(location = 3) in uint in_light;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_distance;

float brightness(float level) {
    float f = level / 15.0;
    return f / (4.0 - 3.0 * f);
}

vec3 lightmap(float sky_level, float block_level) {
    float sky = brightness(sky_level);
    float block = brightness(block_level);
    vec3 warm = vec3(block, block * ((block * 0.6 + 0.4) * 0.6 + 0.4),
                     block * (block * block * 0.6 + 0.4));
    vec3 lit = clamp(warm + vec3(sky * 0.99, sky, sky), 0.0, 1.0);
    return lit * 0.96 + 0.03;
}

void main() {
    gl_Position = push.view_projection * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_distance = length(in_position.xz);
    vec3 light = (in_light & 0x100u) != 0u
        ? vec3(1.0)
        : lightmap(float(in_light & 0xFu), float((in_light >> 4) & 0xFu));
    out_colour = vec4(in_colour.rgb * light, in_colour.a);
}
