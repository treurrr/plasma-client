#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;   // the section's corner in world space
    vec4 eye;      // camera in world space, w is where the fog starts
    vec4 sky;      // what the fog fades into, w is where it finishes
} push;

layout(location = 0) in uvec4 in_position;   // xyz fixed point, w packed flags
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uvec2 in_texture_light;

const float kPositionScale = 2048.0;
const float kPositionBias = 1.0;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out float out_shade;
layout(location = 2) out vec3 out_tint;
layout(location = 3) out float out_distance;
layout(location = 4) out vec3 out_light;

// Minecraft shades a face by its direction alone.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

// The game's own light curve: the last few levels fall away far faster than
// the first ones, which is why a torch lights a small bright circle rather
// than a wide dim one.
float brightness(float level) {
    float f = level / 15.0;
    return f / (4.0 - 3.0 * f);
}

// Block light is warm and sky light is not, so a torch-lit wall and a sunlit
// one do not end up the same colour.
vec3 lightmap(float sky_level, float block_level) {
    float sky = brightness(sky_level);
    float block = brightness(block_level);
    vec3 warm = vec3(block, block * ((block * 0.6 + 0.4) * 0.6 + 0.4),
                     block * (block * block * 0.6 + 0.4));
    vec3 lit = clamp(warm + vec3(sky * 0.99, sky, sky), 0.0, 1.0);
    return lit * 0.96 + 0.03;
}

void main() {
    vec3 local = vec3(in_position.xyz) / kPositionScale - kPositionBias;
    vec3 world = local + push.origin.xyz;
    gl_Position = push.view_projection * vec4(world, 1.0);
    // Horizontal distance, the way the game measures it: fog should not thicken
    // when you look down from a tower.
    out_distance = length(world.xz - push.eye.xz);

    out_uv = vec3(in_uv, float(in_texture_light.x));
    uint flags = in_position.w;
    uint face = flags & 7u;

    // Smooth lighting: 0 is a fully enclosed corner, 3 is open.
    const float kCornerLight[4] = float[](0.52, 0.68, 0.84, 1.0);
    out_shade = kFaceShade[min(face, 5u)] * kCornerLight[(flags >> 5) & 3u];
    out_light = lightmap(float(in_texture_light.y & 0x0Fu),
                         float((in_texture_light.y >> 4) & 0x0Fu));

    // Grass, foliage and water ship as greyscale and are multiplied by a biome
    // colour. Biomes are parsed but not yet resolved, so these are the plains
    // values.
    out_tint = vec3(1.0);
    if ((flags & 0x08u) != 0u) out_tint = vec3(0.569, 0.741, 0.349);
    if ((flags & 0x10u) != 0u) out_tint = vec3(0.247, 0.463, 0.894);
}
