#version 450

// Fancy clouds: every filled texel of the cloud sheet is a box of 12x4x12
// blocks. The ray from the eye walks the grid of boxes to the first face it
// meets, shades it by which way it faces, and writes that face's depth so the
// terrain in front of a cloud still covers it.
layout(set = 0, binding = 0) uniform sampler2D sheet;

layout(set = 0, binding = 2) uniform Clouds {
    vec4 forward;    // xyz, w is tan(fov/2)
    vec4 right;      // xyz, w is the aspect ratio
    vec4 up;
    vec4 eye;        // xyz, w is the height of the layer's floor
    vec4 colour;     // rgba
    vec4 params;     // x, z how far the sheet has drifted, w where clouds fade out
    mat4 view_projection;
} clouds;

layout(location = 0) in vec2 in_ndc;
layout(location = 0) out vec4 out_colour;

const vec3 kCell = vec3(12.0, 4.0, 12.0);
// Down, up, north, south, west, east, as the game shades them.
const float kShade[6] = float[](0.7, 1.0, 0.8, 0.8, 0.9, 0.9);

bool filled(ivec2 cell) {
    return texelFetch(sheet, ivec2(cell.x & 255, cell.y & 255), 0).r > 0.5;
}

void main() {
    vec3 d = normalize(clouds.forward.xyz +
                       clouds.right.xyz * in_ndc.x * clouds.forward.w * clouds.right.w -
                       clouds.up.xyz * in_ndc.y * clouds.forward.w);
    vec3 o = clouds.eye.xyz;
    float floor_y = clouds.eye.w;
    float top_y = floor_y + kCell.y;
    float fade = clouds.params.w;

    float t_enter;
    float t_exit;
    if (abs(d.y) < 1e-6) {
        if (o.y < floor_y || o.y > top_y) discard;
        t_enter = 0.0;
        t_exit = fade;
    } else {
        float ta = (floor_y - o.y) / d.y;
        float tb = (top_y - o.y) / d.y;
        t_enter = max(min(ta, tb), 0.0);
        t_exit = max(ta, tb);
    }
    if (t_exit <= t_enter || t_enter >= fade) discard;
    bool in_layer = o.y >= floor_y && o.y <= top_y;

    vec2 drift = clouds.params.xy;
    vec2 start = (o.xz + d.xz * t_enter + drift) / kCell.xz;
    ivec2 cell = ivec2(floor(start));
    bool inside = filled(cell);

    int face = -1;
    float t_hit = 0.0;
    if (inside && !in_layer) {
        face = d.y > 0.0 ? 0 : 1;
        t_hit = t_enter;
    } else {
        ivec2 step_by = ivec2(d.x >= 0.0 ? 1 : -1, d.z >= 0.0 ? 1 : -1);
        vec2 delta = vec2(abs(d.x) > 1e-9 ? kCell.x / abs(d.x) : 1e30,
                          abs(d.z) > 1e-9 ? kCell.z / abs(d.z) : 1e30);
        vec2 next_edge = vec2(cell + max(step_by, ivec2(0)));
        vec2 t_max = vec2(abs(d.x) > 1e-9 ? (next_edge.x - start.x) * kCell.x / d.x : 1e30,
                          abs(d.z) > 1e-9 ? (next_edge.y - start.y) * kCell.z / d.z : 1e30) +
                     t_enter;
        float limit = min(t_exit, fade);
        for (int i = 0; i < 512; ++i) {
            bool across_x = t_max.x < t_max.y;
            float t = across_x ? t_max.x : t_max.y;
            if (t > limit) break;
            if (across_x) {
                cell.x += step_by.x;
                t_max.x += delta.x;
            } else {
                cell.y += step_by.y;
                t_max.y += delta.y;
            }
            if (filled(cell) != inside) {
                // Entering a box shows its near side; leaving the one the eye
                // is in shows that box's far side from within.
                if (across_x) face = (step_by.x > 0) == inside ? 5 : 4;
                else face = (step_by.y > 0) == inside ? 3 : 2;
                t_hit = t;
                break;
            }
        }
        if (face < 0 && inside && t_exit < fade) {
            face = d.y > 0.0 ? 1 : 0;
            t_hit = t_exit;
        }
    }
    if (face < 0) discard;

    vec3 hit = o + d * t_hit;
    vec4 clip = clouds.view_projection * vec4(hit, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w, 0.0, 1.0);
    float alpha = clouds.colour.a * (1.0 - clamp(t_hit / fade, 0.0, 1.0));
    out_colour = vec4(clouds.colour.rgb * kShade[face], alpha);
}
