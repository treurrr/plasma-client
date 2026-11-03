#version 450

layout(set = 0, binding = 1) uniform sampler2DArray celestial;

layout(push_constant) uniform Sky {
    mat4 transform;
    vec4 colour;
    vec4 fog;
    vec4 params;
} sky;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in float in_alpha;
layout(location = 2) in float in_distance;
layout(location = 0) out vec4 out_colour;

void main() {
    vec4 colour = sky.colour;
    if (sky.params.x == 1.0) {
        colour *= texture(celestial, vec3(in_uv, sky.params.y));
    } else if (sky.params.x == 2.0) {
        colour.a *= in_alpha;
    }
    // The sky's own fog: nothing at the eye, total at the sky's fog distance,
    // measured along the geometry the way the game measures it.
    if (sky.fog.w > 0.0) {
        float amount = clamp(in_distance / sky.fog.w, 0.0, 1.0);
        colour.rgb = mix(colour.rgb, sky.fog.rgb, amount);
    }
    out_colour = colour;
}
