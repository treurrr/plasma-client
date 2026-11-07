#version 450
// The hand's own pass: everything is already in camera space, so the only
// matrix is the projection and the only light is the world's at the player.

layout(push_constant) uniform Push {
    mat4 projection;
    vec4 light0;
    vec4 light1;
    vec4 lightmap;
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in uint in_layer;
layout(location = 4) in uint in_colour;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) flat out uint out_skin;

void main() {
    gl_Position = push.projection * vec4(in_position, 1.0);
    out_skin = in_layer >> 31;
    out_uv = vec3(in_uv, float(in_layer & 0x7FFFFFFFu));
    vec3 normal = normalize(in_normal);
    float lit = min(1.0, (max(0.0, dot(push.light0.xyz, normal))
                          + max(0.0, dot(push.light1.xyz, normal))) * 0.6 + 0.4);
    vec4 tint = vec4(float(in_colour & 0xFFu), float((in_colour >> 8) & 0xFFu),
                     float((in_colour >> 16) & 0xFFu), float((in_colour >> 24) & 0xFFu)) / 255.0;
    out_colour = vec4(tint.rgb * lit * push.lightmap.rgb, tint.a);
}
