#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 fog;
    vec4 params;
    uvec4 bases;
} push;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in float in_distance;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 colour = texture(atlas, in_uv) * in_colour;
    if (colour.a < 0.004) discard;
    if (push.fog.w > 0.0) {
        float fog = clamp((in_distance - push.params.x) / max(push.fog.w - push.params.x, 1.0),
                          0.0, 1.0);
        colour.rgb = mix(colour.rgb, push.fog.rgb, fog);
    }
    out_color = colour;
}
