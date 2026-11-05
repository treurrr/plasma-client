#version 450

// The world's push block: view_projection, then the camera the particles are
// placed against in origin.
layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 origin;
    vec4 eye;
    vec4 extra;
} push;

layout(set = 0, binding = 3) uniform sampler2D lightmap;

// One instance per particle: QuadParticleRenderState's row.
layout(location = 0) in vec4 in_position_size;
layout(location = 1) in vec4 in_rotation;
layout(location = 2) in vec4 in_uv;          // u0 u1 v0 v1
layout(location = 3) in uvec4 in_colour_light_texture_layer;

layout(location = 0) out vec3 out_uv;
layout(location = 1) out vec4 out_colour;
layout(location = 2) out float out_spherical;
layout(location = 3) out float out_cylindrical;

// The four corners in the game's order, as triangles 0 1 2 and 2 3 0.
const vec2 kCorner[4] = vec2[](vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0), vec2(-1.0, -1.0));
const int kIndex[6] = int[](0, 1, 2, 2, 3, 0);

vec3 rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main() {
    int corner = kIndex[gl_VertexIndex];
    vec2 c = kCorner[corner];
    vec3 relative = rotate(in_rotation, vec3(c, 0.0)) * in_position_size.w + in_position_size.xyz;
    gl_Position = push.view_projection * vec4(relative + push.origin.xyz, 1.0);
    vec3 from_eye = relative + push.origin.xyz - push.eye.xyz;
    out_spherical = length(from_eye);
    out_cylindrical = max(length(from_eye.xz), abs(from_eye.y));

    float u = corner < 2 ? in_uv.y : in_uv.x;
    float v = (corner == 0 || corner == 3) ? in_uv.w : in_uv.z;
    out_uv = vec3(u, v, float(in_colour_light_texture_layer.z));

    uint argb = in_colour_light_texture_layer.x;
    vec4 colour = vec4((argb >> 16) & 0xFFu, (argb >> 8) & 0xFFu, argb & 0xFFu, argb >> 24) / 255.0;
    uint light = in_colour_light_texture_layer.y;
    ivec2 texel = ivec2((light & 0xFFFFu) >> 4, (light >> 16) >> 4);
    out_colour = colour * texelFetch(lightmap, texel, 0);
}
