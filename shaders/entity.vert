#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 fog;       // rgb, and where it finishes
    vec4 params;    // where fog starts, sky light, block light, 1 when lit
    vec4 overlay;
    vec4 colour;
} push;

layout(location = 0) in vec3 in_position;   // relative to the camera
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_colour;
layout(location = 2) out float out_distance;

// Two lights from fixed directions, the way the game shades every entity: a
// face turned to neither gets the ambient four tenths.
const vec3 kLight0 = normalize(vec3(0.2, 1.0, -0.7));
const vec3 kLight1 = normalize(vec3(-0.2, 1.0, 0.7));

float brightness(float level) {
    float f = level / 15.0;
    return f / (4.0 - 3.0 * f);
}

vec3 lightmap(float sky_level, float block_level) {
    float sky = brightness(sky_level);
    float block = brightness(block_level);
    vec3 warm = vec3(block, block * ((block * 0.6 + 0.4) * 0.6 + 0.4),
                     block * (block * block * 0.6 + 0.4));
    vec3 lit = clamp(warm + vec3(sky * 0.99, sky, sky), 0.0, 1.0);
    return lit * 0.96 + 0.03;
}

void main() {
    gl_Position = push.view_projection * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_distance = length(in_position.xz);
    float shade = 1.0;
    if (push.params.w > 0.5) {
        vec3 n = normalize(in_normal);
        shade = min(1.0, (max(dot(kLight0, n), 0.0) + max(dot(kLight1, n), 0.0)) * 0.6 + 0.4);
    }
    out_colour = vec3(shade) * lightmap(push.params.y, push.params.z);
}
