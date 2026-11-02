#version 450

layout(set = 0, binding = 0) uniform sampler2DArray atlas;

layout(location = 0) in vec3 in_uv;
layout(location = 1) in float in_shade;
layout(location = 2) in vec3 in_tint;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 texel = texture(atlas, in_uv);
    if (texel.a < 0.5) discard;
    out_color = vec4(texel.rgb * in_tint * in_shade, 1.0);
}
