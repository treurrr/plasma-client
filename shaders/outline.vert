#version 450

layout(push_constant) uniform Push {
    mat4 view_projection;
    vec4 eye;      // xyz, then the line width in pixels
    vec4 corner;   // the block's corner from the eye
    vec4 colour;
    vec4 screen;
} push;

layout(location = 0) in vec3 edge_start;
layout(location = 1) in vec3 edge_end;

// Each edge is a band of two triangles: the start's two sides, then the end's.
const int kCorner[6] = int[](0, 1, 2, 3, 2, 1);
// The game draws its lines a 256th nearer the eye, so they win against the
// faces they lie on.
const float kViewShrink = 1.0 - 1.0 / 256.0;

void main() {
    const int corner = kCorner[gl_VertexIndex];
    const vec3 normal = normalize(edge_end - edge_start);
    const vec3 position = push.corner.xyz + (corner < 2 ? edge_start : edge_end);
    const vec4 start = push.view_projection * vec4(push.eye.xyz + position * kViewShrink, 1.0);
    const vec4 end = push.view_projection * vec4(push.eye.xyz + (position + normal) * kViewShrink, 1.0);
    const vec3 ndc1 = start.xyz / start.w;
    const vec3 ndc2 = end.xyz / end.w;
    const vec2 direction = normalize((ndc2.xy - ndc1.xy) * push.screen.xy);
    vec2 offset = vec2(-direction.y, direction.x) * push.eye.w / push.screen.xy;
    if (offset.x < 0.0) offset = -offset;
    const vec2 side = (corner % 2 == 0) ? offset : -offset;
    gl_Position = vec4((ndc1 + vec3(side, 0.0)) * start.w, start.w);
}
