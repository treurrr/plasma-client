#version 450

layout(push_constant) uniform Clouds {
    mat4 transform;
    vec4 colour;
    vec4 offset;
    vec4 cell;
} clouds;

layout(location = 0) in vec4 in_colour;
layout(location = 1) in float in_distance;
layout(location = 0) out vec4 out_colour;

void main() {
    float end = clouds.offset.w;
    float fog = in_distance <= 0.0 ? 0.0 : in_distance >= end ? 1.0 : in_distance / end;
    out_colour = vec4(in_colour.rgb, in_colour.a * (1.0 - fog));
}
