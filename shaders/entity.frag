#version 450

layout(set = 0, binding = 0) uniform sampler2D skin;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 fog;
    vec4 params;
    vec4 overlay;
    vec4 colour;
} push;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_colour;
layout(location = 2) in float in_distance;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 texel = texture(skin, in_uv) * push.colour;
    if (texel.a < 0.1 * push.colour.a) discard;
    vec3 colour = mix(texel.rgb, push.overlay.rgb, push.overlay.a) * in_colour;
    if (push.fog.w > 0.0) {
        float fog = clamp((in_distance - push.params.x) / max(push.fog.w - push.params.x, 1.0),
                          0.0, 1.0);
        colour = mix(colour, push.fog.rgb, fog);
    }
    out_color = vec4(colour, texel.a);
}
