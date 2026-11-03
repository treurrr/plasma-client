#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 block;
    vec4 colour;
} push;

layout(location = 0) out vec4 out_color;

void main() { out_color = push.colour; }
