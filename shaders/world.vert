#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    uvec4 atlas;   // columns, tile size, atlas width, atlas height
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_texture;
layout(location = 3) in uint in_face;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out float out_shade;
layout(location = 2) out vec3 out_tint;

// Minecraft shades a face by its direction alone.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

void main() {
    gl_Position = push.view_projection * vec4(in_position, 1.0);

    float tile = float(push.atlas.y);
    vec2 origin = vec2(float(in_texture % push.atlas.x), float(in_texture / push.atlas.x)) * tile;
    out_uv = (origin + clamp(in_uv, 0.0, 1.0) * tile) / vec2(push.atlas.zw);
    uint face = in_face & 0xFFu;
    out_shade = kFaceShade[min(face, 5u)];

    // Grass, foliage and stems ship as greyscale and are multiplied by a biome
    // colour. Biomes are parsed but not yet resolved, so this is the plains
    // value for every tinted face.
    out_tint = (in_face & 0x100u) != 0u ? vec3(0.569, 0.741, 0.349) : vec3(1.0);
}
