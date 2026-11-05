#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;
    vec4 eye;
    vec4 extra;
} push;

layout(location = 0) in vec3 in_position;   // relative to origin
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_stage;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out float out_spherical;
layout(location = 2) out float out_cylindrical;

void main() {
    vec3 world = in_position + push.origin.xyz;
    gl_Position = push.view_projection * vec4(world, 1.0);
    vec3 from_eye = world - push.eye.xyz;
    out_spherical = length(from_eye);
    out_cylindrical = max(length(from_eye.xz), abs(from_eye.y));
    out_uv = vec3(in_uv, float(in_stage));
}
