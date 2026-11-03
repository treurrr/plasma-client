#version 450

// The sky is geometry around the camera, as the game builds it: a disc
// overhead, a fan of glow at the horizon, the sun and moon as quads, the
// stars as small quads, a dark disc below. The camera sits at the origin.
layout(push_constant) uniform Sky {
    mat4 transform;   // projection, view rotation and the piece's own turn
    vec4 colour;
    vec4 fog;         // rgb, w is where the fog is total; 0 for none
    vec4 params;      // x the mode, y the texture layer, z the glow's alpha
} sky;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_uv_alpha;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out float out_alpha;
layout(location = 2) out float out_distance;

void main() {
    vec3 position = in_position;
    // The glow's ring leans towards the sun by how strong the glow is.
    if (sky.params.x == 2.0) position.z *= sky.params.z;
    gl_Position = sky.transform * vec4(position, 1.0);
    out_uv = in_uv_alpha.xy;
    out_alpha = in_uv_alpha.z;
    out_distance = length(position);
}
