#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(set = 0, binding = 1) uniform sampler2DArray skins;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 fog;
    vec4 params;
    uvec4 bases;
} push;

layout(location = 0) in vec3 in_uv;
layout(location = 1) in vec4 in_front;
layout(location = 2) in vec4 in_back;
layout(location = 3) in vec4 in_overlay;
layout(location = 4) in vec3 in_light;
layout(location = 5) in float in_distance;
layout(location = 6) flat in uint in_skin;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 colour = in_skin != 0u ? texture(skins, in_uv) : texture(atlas, in_uv.xy);
    if (colour.a < 0.1) discard;
    colour *= gl_FrontFacing ? in_front : in_back;
    colour.rgb = mix(in_overlay.rgb, colour.rgb, in_overlay.a);
    colour.rgb *= in_light;
    if (push.fog.w > 0.0) {
        float fog = clamp((in_distance - push.params.x) / max(push.fog.w - push.params.x, 1.0),
                          0.0, 1.0);
        colour.rgb = mix(colour.rgb, push.fog.rgb, fog);
    }
    out_color = colour;
}
