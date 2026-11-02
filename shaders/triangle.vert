#version 450

layout(location = 0) out vec3 vertex_color;

const vec2 kPositions[3] = vec2[](vec2(0.0, -0.7), vec2(0.7, 0.7), vec2(-0.7, 0.7));
const vec3 kColors[3] = vec3[](vec3(1.0, 0.30, 0.34), vec3(0.2, 1.0, 0.4), vec3(0.3, 0.5, 1.0));

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    vertex_color = kColors[gl_VertexIndex];
}
