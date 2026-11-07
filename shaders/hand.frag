#version 450

layout(set = 0, binding = 0) uniform sampler2DArray items;
layout(set = 0, binding = 1) uniform sampler2DArray skin;

layout(location = 0) in vec3 in_uv;
layout(location = 1) in vec4 in_colour;
layout(location = 2) flat in uint in_skin;
layout(location = 0) out vec4 out_colour;

void main() {
    vec4 colour = in_skin != 0u ? texture(skin, vec3(in_uv.xy, 0.0)) : texture(items, in_uv);
    if (colour.a < 0.01) discard;
    out_colour = vec4(colour.rgb * in_colour.rgb, colour.a * in_colour.a);
}
