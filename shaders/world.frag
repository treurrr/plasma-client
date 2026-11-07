#version 450

layout(set = 0, binding = 0) uniform sampler2DArray atlas;

layout(set = 0, binding = 2) uniform Fog {
    vec4 colour;
    vec4 distances;   // environmental start and end, render distance start and end
} fog;

// Per texture: the layer it shows now and the next in the low and high half,
// and how far an interpolating one has faded towards the next.
layout(set = 0, binding = 4) readonly buffer Animation {
    uvec2 frames[];
} animation;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;
    vec4 eye;
    vec4 extra;   // x the atlas tile size in texels, y 1 for rotated grid sampling
} push;

layout(location = 0) in vec3 in_uv;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in float in_spherical;
layout(location = 3) in float in_cylindrical;
layout(location = 0) out vec4 out_color;

float linear_fog(float distance, float start, float end) {
    if (distance <= start) return 0.0;
    if (distance >= end) return 1.0;
    return (distance - start) / (end - start);
}

// `terrain.fsh sampleNearest`: pull the sample towards the centre of its texel
// by as much as the texel is wider than a screen pixel, so a texel stays hard
// edged up close and goes smooth only once it no longer covers a pixel.
vec4 sample_nearest(vec2 uv, float layer, vec2 pixel_size, vec2 du, vec2 dv,
                    vec2 texel_screen_size) {
    vec2 texel_coords = uv / pixel_size;
    vec2 texel_centre = round(texel_coords) - 0.5;
    vec2 texel_offset = texel_coords - texel_centre;
    texel_offset = (texel_offset - 0.5) * pixel_size / texel_screen_size + 0.5;
    texel_offset = clamp(texel_offset, 0.0, 1.0);
    return textureGrad(atlas, vec3((texel_centre + texel_offset) * pixel_size, layer), du, dv);
}

// `terrain.fsh sampleRGSS`: four samples on a rotated grid across two mip
// levels, faded in as soon as a texel shrinks below a screen pixel. The level
// comes from the geometric mean of the two derivatives, so a floor seen at a
// grazing angle keeps the detail an ordinary mip pick would throw away.
vec4 sample_rgss(vec2 uv, float layer, vec2 pixel_size) {
    vec2 du = dFdx(uv);
    vec2 dv = dFdy(uv);
    vec2 texel_screen_size = sqrt(du * du + dv * dv);
    float max_texel_size = max(texel_screen_size.x, texel_screen_size.y);
    float min_pixel_size = min(pixel_size.x, pixel_size.y);
    float blend = smoothstep(min_pixel_size, min_pixel_size * 2.0, max_texel_size);

    float du_length = length(du);
    float dv_length = length(dv);
    float effective = sqrt(min(du_length, dv_length) * max(du_length, dv_length));
    float exact = max(0.0, log2(effective / min_pixel_size));
    float low = floor(exact);

    const vec2 offsets[4] = vec2[](vec2(0.125, 0.375), vec2(-0.125, -0.375),
                                   vec2(0.375, -0.125), vec2(-0.375, 0.125));
    vec4 near_level = vec4(0.0);
    vec4 far_level = vec4(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 at = vec3(uv + offsets[i] * pixel_size, layer);
        near_level += textureLod(atlas, at, low);
        far_level += textureLod(atlas, at, low + 1.0);
    }
    vec4 grid = mix(near_level * 0.25, far_level * 0.25, fract(exact));
    return mix(sample_nearest(uv, layer, pixel_size, du, dv, texel_screen_size), grid, blend);
}

vec4 sample_atlas(vec2 uv, float layer, vec2 pixel_size) {
    if (push.extra.y > 0.5) return sample_rgss(uv, layer, pixel_size);
    vec2 du = dFdx(uv);
    vec2 dv = dFdy(uv);
    return sample_nearest(uv, layer, pixel_size, du, dv, sqrt(du * du + dv * dv));
}

void main() {
    uvec2 frame = animation.frames[uint(in_uv.z + 0.5)];
    vec2 pixel_size = vec2(1.0 / push.extra.x);
    vec4 texel = sample_atlas(in_uv.xy, float(frame.x & 0xFFFFu), pixel_size);
    float fade = uintBitsToFloat(frame.y);
    if (fade > 0.0) {
        texel = mix(texel, sample_atlas(in_uv.xy, float(frame.x >> 16), pixel_size), fade);
    }
    vec4 colour = texel * in_colour;
    if (colour.a < push.eye.w) discard;
    float amount = max(linear_fog(in_spherical, fog.distances.x, fog.distances.y),
                       linear_fog(in_cylindrical, fog.distances.z, fog.distances.w));
    out_color = vec4(mix(colour.rgb, fog.colour.rgb, amount * fog.colour.a), colour.a);
}
