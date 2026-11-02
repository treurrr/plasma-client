#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;   // the section's corner in world space
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_texture;
layout(location = 3) in uint in_face;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out float out_shade;
layout(location = 2) out vec3 out_tint;

// Minecraft shades a face by its direction alone.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

void main() {
    gl_Position = push.view_projection * vec4(in_position + push.origin.xyz, 1.0);

    out_uv = vec3(in_uv, float(in_texture));
    uint face = in_face & 0xFFu;

    // Smooth lighting: 0 is a fully enclosed corner, 3 is open.
    const float kCornerLight[4] = float[](0.52, 0.68, 0.84, 1.0);
    out_shade = kFaceShade[min(face, 5u)] * kCornerLight[(in_face >> 16) & 3u];

    // Grass, foliage and water ship as greyscale and are multiplied by a biome
    // colour. Biomes are parsed but not yet resolved, so these are the plains
    // values.
    out_tint = vec3(1.0);
    if ((in_face & 0x100u) != 0u) out_tint = vec3(0.569, 0.741, 0.349);
    if ((in_face & 0x200u) != 0u) out_tint = vec3(0.247, 0.463, 0.894);
}
