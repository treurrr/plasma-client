#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;   // the section's corner in world space
    vec4 eye;      // camera in world space, w is where the fog starts
    vec4 sky;      // what the fog fades into, w is where it finishes
} push;

layout(location = 0) in uvec4 in_position;   // xyz fixed point, w packed flags
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_texture;

const float kPositionScale = 2048.0;
const float kPositionBias = 1.0;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out float out_shade;
layout(location = 2) out vec3 out_tint;
layout(location = 3) out float out_distance;

// Minecraft shades a face by its direction alone.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

void main() {
    vec3 local = vec3(in_position.xyz) / kPositionScale - kPositionBias;
    vec3 world = local + push.origin.xyz;
    gl_Position = push.view_projection * vec4(world, 1.0);
    // Horizontal distance, the way the game measures it: fog should not thicken
    // when you look down from a tower.
    out_distance = length(world.xz - push.eye.xz);

    out_uv = vec3(in_uv, float(in_texture));
    uint flags = in_position.w;
    uint face = flags & 7u;

    // Smooth lighting: 0 is a fully enclosed corner, 3 is open.
    const float kCornerLight[4] = float[](0.52, 0.68, 0.84, 1.0);
    out_shade = kFaceShade[min(face, 5u)] * kCornerLight[(flags >> 5) & 3u];

    // Grass, foliage and water ship as greyscale and are multiplied by a biome
    // colour. Biomes are parsed but not yet resolved, so these are the plains
    // values.
    out_tint = vec3(1.0);
    if ((flags & 0x08u) != 0u) out_tint = vec3(0.569, 0.741, 0.349);
    if ((flags & 0x10u) != 0u) out_tint = vec3(0.247, 0.463, 0.894);
}
