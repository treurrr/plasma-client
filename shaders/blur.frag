#version 450

layout(location = 0) in vec2 uv;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Blur {
    vec2 texel;
    vec2 direction;
    float radius;
} blur;

layout(location = 0) out vec4 colour;

// The game's box blur: linear filtering reads two texels per tap halfway
// between them, and the last texel of the odd-sized box comes in at half
// weight, so 2r + 1 texels cost r + 1 taps.
void main() {
    vec2 step_by = blur.texel * blur.direction;
    vec4 sum = vec4(0.0);
    for (float a = -blur.radius + 0.5; a <= blur.radius; a += 2.0) {
        sum += texture(source, uv + step_by * a);
    }
    sum += texture(source, uv + step_by * blur.radius) / 2.0;
    colour = sum / (blur.radius + 0.5);
}
