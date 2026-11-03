#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 block;   // the block's lower corner in world space
    vec4 colour;
} push;

// Twelve edges of a cube, so the outline needs no vertex buffer.
const vec3 kCorner[8] = vec3[](vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 0, 1), vec3(0, 0, 1),
                               vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(0, 1, 1));
const int kEdge[24] = int[](0, 1, 1, 2, 2, 3, 3, 0,
                            4, 5, 5, 6, 6, 7, 7, 4,
                            0, 4, 1, 5, 2, 6, 3, 7);

void main() {
    // Grown a hair so the line sits outside the face rather than fighting it.
    const float grow = 0.002;
    vec3 local = kCorner[kEdge[gl_VertexIndex]] * (1.0 + 2.0 * grow) - grow;
    gl_Position = push.view_projection * vec4(push.block.xyz + local, 1.0);
}
