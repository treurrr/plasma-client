#version 450

layout(set = 0, binding = 2) uniform Fog {
    vec4 colour;
    vec4 distances;
} fog;

// rain.png as layer 0 and snow.png as layer 1, repeating and unfiltered.
layout(set = 1, binding = 0) uniform sampler2DArray weather;

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
    vec4 texel = texture(weather, in_uv) * in_colour;
    if (texel.a < 0.1) discard;
    float amount = max(linear_fog(in_spherical, fog.distances.x, fog.distances.y),
                       linear_fog(in_cylindrical, fog.distances.z, fog.distances.w));
    out_color = vec4(mix(texel.rgb, fog.colour.rgb, amount * fog.colour.a), texel.a);
}
