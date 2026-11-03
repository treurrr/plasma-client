#version 450

layout(set = 0, binding = 0) uniform sampler2D clouds;
layout(set = 0, binding = 1) uniform sampler2D sun;

layout(push_constant) uniform Sky {
    vec4 forward;    // xyz, w is tan(fov/2)
    vec4 right;      // xyz, w is the aspect ratio
    vec4 up;         // xyz, w is how far the cloud deck sits above the eye
    vec4 eye;        // xyz, w is how far the deck has drifted
    vec4 zenith;     // rgb, w is the distance clouds fade out over
    vec4 horizon;    // rgb, w is the cosine of the sun's radius
    vec4 sun_dir;    // xyz, w is unused
} sky;

layout(location = 0) in vec2 in_ndc;
layout(location = 0) out vec4 out_colour;

// Twelve blocks to a texel, so the sheet repeats every 3072 blocks, which is
// what the game uses and what makes the clouds the size they are.
const float kBlocksPerTexel = 12.0;

void main() {
    vec3 direction = normalize(sky.forward.xyz +
                               sky.right.xyz * in_ndc.x * sky.forward.w * sky.right.w -
                               sky.up.xyz * in_ndc.y * sky.forward.w);

    // The band near the horizon is the fog colour, so the world fades into the
    // sky rather than into a different blue.
    float height = clamp(direction.y, -1.0, 1.0);
    vec3 colour = mix(sky.horizon.rgb, sky.zenith.rgb, smoothstep(-0.05, 0.35, height));

    float towards_sun = dot(direction, sky.sun_dir.xyz);
    if (towards_sun > sky.horizon.w) {
        // Flatten the cone around the sun into the square the texture wants.
        vec3 side = normalize(cross(sky.sun_dir.xyz, vec3(0.0, 1.0, 0.0)));
        vec3 lift = cross(side, sky.sun_dir.xyz);
        float radius = sqrt(max(1.0 - sky.horizon.w * sky.horizon.w, 1e-5));
        vec2 uv = vec2(dot(direction, side), dot(direction, lift)) / radius * 0.5 + 0.5;
        vec4 disc = texture(sun, clamp(uv, 0.0, 1.0));
        colour = mix(colour, disc.rgb, disc.a);
    }

    if (direction.y > 0.001 && sky.up.w > 0.0) {
        float travel = sky.up.w / direction.y;
        vec2 where = sky.eye.xz + direction.xz * travel + vec2(sky.eye.w, 0.0);
        float cover = step(0.5, texture(clouds, where / (kBlocksPerTexel * 256.0)).r);
        // Clouds thin out with distance instead of tiling to the horizon.
        float near = 1.0 - smoothstep(sky.zenith.w * 0.4, sky.zenith.w, travel);
        // Seen from below the deck is its underside: seven tenths of white,
        // laid over the sky at the alpha the game uses.
        colour = mix(colour, vec3(0.7), cover * near * 0.8);
    }

    out_colour = vec4(colour, 1.0);
}
