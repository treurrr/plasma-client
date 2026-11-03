#version 450

layout(set = 0, binding = 0) uniform sampler2DArray atlas;

layout(location = 0) in vec3 in_uv;
layout(location = 1) in float in_shade;
layout(location = 2) in vec3 in_tint;
layout(location = 3) in float in_distance;
layout(location = 4) in vec3 in_light;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;
    vec4 eye;
    vec4 sky;
} push;

void main() {
    vec4 texel = texture(atlas, in_uv);
    // The game's own cutout threshold; 0.5 eats thin geometry at a distance.
    if (texel.a < 0.1) discard;
    vec3 colour = texel.rgb * in_tint * in_shade * in_light;
    if (push.sky.w > 0.0) {
        float fog = clamp((in_distance - push.eye.w) / max(push.sky.w - push.eye.w, 1.0),
                          0.0, 1.0);
        colour = mix(colour, push.sky.rgb, fog * fog);
    }
    out_color = vec4(colour, texel.a);
}
