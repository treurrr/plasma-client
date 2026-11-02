#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 tint;

layout(set = 0, binding = 0) uniform sampler2D glyphs;

layout(location = 0) out vec4 colour;

void main() {
    // The atlas stores coverage, so the colour comes from the vertex.
    float coverage = texture(glyphs, uv).r;
    if (coverage <= 0.003) discard;
    colour = vec4(tint.rgb, tint.a * coverage);
}
