#version 450

// One cloud face per six vertices, read from the face list the cpu builds:
// the cell relative to the camera's cell, the face and how to shade it.
layout(set = 0, binding = 0) readonly buffer Faces {
    uint faces[];
} cloud_faces;

layout(push_constant) uniform Clouds {
    mat4 transform;
    vec4 colour;
    vec4 offset;   // xyz where the camera's cell starts, w where the fog ends
    vec4 cell;
} clouds;

layout(location = 0) out vec4 out_colour;
layout(location = 1) out float out_distance;

const vec3 kCorners[24] = vec3[](
    vec3(1, 0, 0), vec3(1, 0, 1), vec3(0, 0, 1), vec3(0, 0, 0),
    vec3(0, 1, 0), vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 1, 0),
    vec3(0, 0, 0), vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 0, 0),
    vec3(1, 0, 1), vec3(1, 1, 1), vec3(0, 1, 1), vec3(0, 0, 1),
    vec3(0, 0, 1), vec3(0, 1, 1), vec3(0, 1, 0), vec3(0, 0, 0),
    vec3(1, 0, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(1, 0, 1));
const float kShade[6] = float[](0.7, 1.0, 0.8, 0.8, 0.9, 0.9);
const int kQuad[6] = int[](0, 1, 2, 2, 3, 0);

void main() {
    uint face = uint(gl_VertexIndex) / 6u;
    int corner = kQuad[gl_VertexIndex % 6];
    uint cell = cloud_faces.faces[face * 2u];
    uint flags = cloud_faces.faces[face * 2u + 1u];
    int x = int(cell << 16) >> 16;
    int z = int(cell) >> 16;
    int direction = int(flags & 7u);
    bool inside = (flags & 16u) != 0u;
    bool top_colour = (flags & 32u) != 0u;
    vec3 corner_position = kCorners[direction * 4 + (inside ? 3 - corner : corner)];
    vec3 position = corner_position * clouds.cell.xyz + vec3(x, 0, z) * clouds.cell.xyz +
                    clouds.offset.xyz;
    gl_Position = clouds.transform * vec4(position, 1.0);
    out_distance = length(position);
    out_colour = vec4(vec3(kShade[top_colour ? 1 : direction]), 1.0) * clouds.colour;
}
