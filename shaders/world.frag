#version 450

layout(set = 0, binding = 0) uniform sampler2DArray atlas;

layout(set = 0, binding = 2) uniform Fog {
    vec4 colour;
    vec4 distances;   // environmental start and end, render distance start and end
} fog;

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;
    vec4 eye;
    vec4 extra;
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

void main() {
    vec4 colour = texture(atlas, in_uv) * in_colour;
    if (colour.a < push.eye.w) discard;
    float amount = max(linear_fog(in_spherical, fog.distances.x, fog.distances.y),
                       linear_fog(in_cylindrical, fog.distances.z, fog.distances.w));
    out_color = vec4(mix(colour.rgb, fog.colour.rgb, amount * fog.colour.a), colour.a);
}
