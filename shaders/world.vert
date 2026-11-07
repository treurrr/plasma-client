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

layout(location = 0) in uvec4 in_position;   // xyz fixed point, w tint and brightness
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uvec2 in_texture_light;
layout(location = 3) in uvec2 in_biome;

const float kPositionScale = 2048.0;
const float kPositionBias = 1.0;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_spherical;
layout(location = 3) out float out_cylindrical;

vec3 unpack(uint rgb) {
    return vec3((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu) / 255.0;
}

// `terrain.vsh minecraft_sample_lightmap`: block light across, sky light down.
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
    // Ambient occlusion and the face's own shade, already multiplied together
    // while the chunk was meshed.
    float shade = float(flags >> 8) / 255.0;

    const vec3 kFixedTint[8] = vec3[](vec3(1.0), vec3(1.0), vec3(1.0), vec3(1.0),
                                      vec3(0.3804, 0.6000, 0.3804),   // spruce
                                      vec3(0.5020, 0.6549, 0.3333),   // birch
                                      vec3(0.1255, 0.5020, 0.1882),   // lily pad
                                      vec3(1.0));
    const int kBiomeChannel[8] = int[](-1, 0, 1, 2, -1, -1, -1, 3);
    uint tint = flags & 7u;
    int channel = kBiomeChannel[tint];
    vec3 tint_colour = channel < 0
        ? kFixedTint[tint]
        : unpack(biomes.colour[in_biome.x & 0xFFu][channel]);

    uint block = in_texture_light.y & 0xFFu;
    uint sky = (in_texture_light.y >> 8) & 0xFFu;
    out_colour = vec4(tint_colour * shade, 1.0) * sample_lightmap(vec2(block, sky));
}
