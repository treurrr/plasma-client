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
    // A model may ask for no directional shading at all, which is what keeps
    // grass and flowers the same colour from every side.
    float directional = (flags & 0x0100u) != 0u ? 1.0 : kFaceShade[min(face, 5u)];
    out_shade = directional * kCornerLight[(flags >> 6) & 3u];
    out_light = lightmap(float(in_texture_light.y & 0x0Fu),
                         float((in_texture_light.y >> 4) & 0x0Fu));

    // Tinted faces ship as greyscale and are multiplied by a colour the block
    // decides. Three of them never vary; the rest come from the biome.
    const vec3 kTint[8] = vec3[](vec3(1.0),
                                 vec3(0.5686, 0.7412, 0.3490),   // grass, plains
                                 vec3(0.4667, 0.6706, 0.1843),   // foliage, plains
                                 vec3(0.2471, 0.4627, 0.8941),   // water
                                 vec3(0.3804, 0.6000, 0.3804),   // spruce
                                 vec3(0.5020, 0.6549, 0.3333),   // birch
                                 vec3(0.1255, 0.5020, 0.1882),   // lily pad
                                 vec3(0.5608, 0.4784, 0.2118));  // dry foliage
    out_tint = kTint[(flags >> 3) & 7u];
}
