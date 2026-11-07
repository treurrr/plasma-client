#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;   // the camera, which the columns are already relative to
    vec4 eye;
    vec4 extra;    // x: 0 for rain, 1 for snow
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_colour;   // ARGB, white with the column's alpha
layout(location = 3) in uint in_light;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_spherical;
layout(location = 3) out float out_cylindrical;

layout(set = 0, binding = 3) uniform sampler2D lightmap;

// `terrain.vsh minecraft_sample_lightmap`: block light across, sky light down.
vec4 sample_lightmap(vec2 uv) {
    return texture(lightmap, clamp(uv / 256.0 + 0.5 / 16.0, vec2(0.5 / 16.0), vec2(15.5 / 16.0)));
}

void main() {
    vec3 world = in_position + push.origin.xyz;
    gl_Position = push.view_projection * vec4(world, 1.0);
    vec3 from_eye = world - push.eye.xyz;
    out_spherical = length(from_eye);
    out_cylindrical = max(length(from_eye.xz), abs(from_eye.y));
    out_uv = vec3(in_uv, push.extra.x);

    vec4 tint = vec4(float((in_colour >> 16) & 0xFFu), float((in_colour >> 8) & 0xFFu),
                     float(in_colour & 0xFFu), float((in_colour >> 24) & 0xFFu)) / 255.0;
    uint block = in_light & 0xFFFFu;
    uint sky = (in_light >> 16) & 0xFFFFu;
    out_colour = tint * sample_lightmap(vec2(float(block & 0xFFu), float(sky & 0xFFu)));
}
