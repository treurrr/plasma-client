#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_texture;
layout(location = 3) in uint in_face;

layout(location = 0) out vec3 out_color;

// Minecraft shades a face by its direction alone; matching that keeps the
// silhouette readable before any texture exists.
const float kFaceShade[6] = float[](0.50, 1.00, 0.80, 0.80, 0.60, 0.60);

vec3 hue_from_texture(uint id) {
    float h = fract(float(id) * 0.6180339887);
    vec3 k = vec3(1.0, 2.0 / 3.0, 1.0 / 3.0);
    vec3 p = abs(fract(vec3(h) + k) * 6.0 - 3.0);
    return mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), 0.55);
}

void main() {
    gl_Position = push.view_projection * vec4(in_position, 1.0);
    out_color = hue_from_texture(in_texture) * kFaceShade[min(in_face, 5u)];
}
