#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;   // the section's corner in world space
    vec4 eye;      // camera in world space, w the alpha below which a texel is cut
    vec4 extra;
} push;

// Four colours per biome - grass, foliage, water, dry foliage - each 0xRRGGBB,
// in the order the server listed them.
layout(set = 0, binding = 1) uniform Biomes {
    uvec4 colour[256];
} biomes;

// The game's lightmap: block light along u, sky light along v, filtered.
layout(set = 0, binding = 3) uniform sampler2D lightmap;

layout(location = 0) in uvec4 in_position;   // xyz fixed point, w packed flags
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uvec2 in_texture_light;

const float kPositionScale = 2048.0;
const float kPositionBias = 1.0;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_spherical;
layout(location = 3) out float out_cylindrical;

// Minecraft shades a face by its direction alone.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

vec3 unpack(uint rgb) {
    return vec3((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu) / 255.0;
}

vec4 sample_lightmap(vec2 uv) {
    return texture(lightmap, clamp(uv / 256.0 + 0.5 / 16.0, vec2(0.5 / 16.0), vec2(15.5 / 16.0)));
}

void main() {
    vec3 local = vec3(in_position.xyz) / kPositionScale - kPositionBias;
    vec3 world = local + push.origin.xyz;
    gl_Position = push.view_projection * vec4(world, 1.0);
    vec3 relative = world - push.eye.xyz;
    out_spherical = length(relative);
    out_cylindrical = max(length(relative.xz), abs(relative.y));

    out_uv = vec3(in_uv, float(in_texture_light.x));
    uint flags = in_position.w;
    uint face = flags & 7u;

    const float kCornerLight[4] = float[](0.52, 0.68, 0.84, 1.0);
    float directional = (flags & 0x0100u) != 0u ? 1.0 : kFaceShade[min(face, 5u)];
    float shade = directional * kCornerLight[(flags >> 6) & 3u];

    const vec3 kFixedTint[8] = vec3[](vec3(1.0), vec3(1.0), vec3(1.0), vec3(1.0),
                                      vec3(0.3804, 0.6000, 0.3804),   // spruce
                                      vec3(0.5020, 0.6549, 0.3333),   // birch
                                      vec3(0.1255, 0.5020, 0.1882),   // lily pad
                                      vec3(1.0));
    const int kBiomeChannel[8] = int[](-1, 0, 1, 2, -1, -1, -1, 3);
    uint tint = (flags >> 3) & 7u;
    int channel = kBiomeChannel[tint];
    vec3 tint_colour = channel < 0
        ? kFixedTint[tint]
        : unpack(biomes.colour[(in_texture_light.y >> 8) & 0xFFu][channel]);

    uint sky = in_texture_light.y & 0x0Fu;
    uint block = (in_texture_light.y >> 4) & 0x0Fu;
    out_colour = vec4(tint_colour * shade, 1.0) * sample_lightmap(vec2(block * 16u, sky * 16u));
}
