#version 450

layout(location = 0) out vec2 out_ndc;

void main() {
    // One triangle that covers the screen; no vertex buffer, no geometry.
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    out_ndc = corner * 2.0 - 1.0;
    gl_Position = vec4(out_ndc, 0.0, 1.0);
}
