#version 450

// The sky's pieces as the game draws them around the eye: the disc, the
// sunrise fan, the sun, moon and stars, the end's box.
layout(push_constant) uniform Sky {
    mat4 transform;
    vec4 colour;
    vec4 fog_colour;
    vec4 params;   // x the mode, y the texture layer, z the sky fog's end
} sky;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_colour;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_spherical;
layout(location = 3) out float out_cylindrical;

void main() {
    gl_Position = sky.transform * vec4(in_position, 1.0);
    out_uv = in_uv;
    out_colour = in_colour;
    // Measured on the piece as built, before it is placed.
    out_spherical = length(in_position);
    out_cylindrical = max(length(in_position.xz), abs(in_position.y));
}
