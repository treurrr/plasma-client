#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;   // camera-relative
    vec4 fog;               // rgb, and where it finishes
    vec4 params;            // where fog starts
    uvec4 bases;            // this frame's first matrix and first instance
} push;

struct Instance {
    uint matrix_base;
    uint texture;   // an atlas rectangle, or a skin layer with the top bit set
    uint light;     // sky | block << 4 | white << 8 | red << 12
    uint colour;    // ARGB
};

layout(std430, set = 0, binding = 2) readonly buffer Matrices { vec4 rows[]; };
layout(std430, set = 0, binding = 3) readonly buffer Instances { Instance instances[]; };
layout(std430, set = 0, binding = 4) readonly buffer Rects { vec4 rects[]; };

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_normal;
layout(location = 3) in uint in_part;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_front;
layout(location = 2) out vec4 out_back;
layout(location = 3) out vec4 out_overlay;
layout(location = 4) out vec3 out_light;
layout(location = 5) out float out_distance;
layout(location = 6) flat out uint out_skin;

const vec3 kLight0 = normalize(vec3(0.2, 1.0, -0.7));
const vec3 kLight1 = normalize(vec3(-0.2, 1.0, 0.7));

float brightness(float level) {
    float f = level / 15.0;
    return f / (4.0 - 3.0 * f);
}

// The same curve the world's blocks are lit by.
vec3 lightmap(float sky_level, float block_level) {
    float sky = brightness(sky_level);
    float block = brightness(block_level);
    vec3 warm = vec3(block, block * ((block * 0.6 + 0.4) * 0.6 + 0.4),
                     block * (block * block * 0.6 + 0.4));
    vec3 lit = clamp(warm + vec3(sky * 0.99, sky, sky), 0.0, 1.0);
    return lit * 0.96 + 0.03;
}

vec4 mix_light(vec2 light, vec4 colour) {
    vec2 value = max(vec2(0.0), light);
    float accum = min(1.0, (value.x + value.y) * 0.6 + 0.4);
    return vec4(colour.rgb * accum, colour.a);
}

void main() {
    Instance instance = instances[gl_InstanceIndex + push.bases.y];
    uint row = (instance.matrix_base + push.bases.x + in_part) * 3u;
    vec4 r0 = rows[row];
    vec4 r1 = rows[row + 1u];
    vec4 r2 = rows[row + 2u];
    vec4 local = vec4(in_position, 1.0);
    vec3 position = vec3(dot(r0, local), dot(r1, local), dot(r2, local));
    gl_Position = push.view_projection * vec4(position, 1.0);
    out_distance = length(position.xz);

    // The inverse transpose up to scale, so a squashed part still lights
    // the way its faces point.
    vec3 c0 = vec3(r0.x, r1.x, r2.x);
    vec3 c1 = vec3(r0.y, r1.y, r2.y);
    vec3 c2 = vec3(r0.z, r1.z, r2.z);
    mat3 cofactor = mat3(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    vec3 normal = cofactor * in_normal.xyz;
    float length_squared = dot(normal, normal);
    normal = length_squared > 0.0 ? normal * inversesqrt(length_squared) : vec3(0.0, 1.0, 0.0);

    vec4 colour = vec4((instance.colour >> 16) & 0xFFu, (instance.colour >> 8) & 0xFFu,
                       instance.colour & 0xFFu, (instance.colour >> 24) & 0xFFu) / 255.0;
    vec2 light = vec2(dot(kLight0, normal), dot(kLight1, normal));
    out_front = mix_light(light, colour);
    out_back = mix_light(-light, colour);

    uint white = (instance.light >> 8) & 0xFu;
    if (((instance.light >> 12) & 1u) != 0u) {
        out_overlay = vec4(1.0, 0.0, 0.0, 178.0 / 255.0);
    } else {
        float alpha = floor((1.0 - float(white) / 15.0 * 0.75) * 255.0) / 255.0;
        out_overlay = vec4(1.0, 1.0, 1.0, alpha);
    }
    out_light = lightmap(float(instance.light & 0xFu), float((instance.light >> 4) & 0xFu));

    if ((instance.texture & 0x80000000u) != 0u) {
        out_uv = vec3(in_uv, float(instance.texture & 0xFFFFu));
        out_skin = 1u;
    } else {
        vec4 rect = rects[instance.texture];
        out_uv = vec3(rect.xy + in_uv * rect.zw, 0.0);
        out_skin = 0u;
    }
}
