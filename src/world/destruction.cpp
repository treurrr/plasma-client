#include "mcpp/world/destruction.hpp"

#include "mcpp/world/mesher.hpp"

#include <cmath>

namespace mcpp::world {
namespace {

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

float sin_f(float a) { return static_cast<float>(std::sin(static_cast<double>(a))); }
float cos_f(float a) { return static_cast<float>(std::cos(static_cast<double>(a))); }

// JOML's rotationX: a turn about x.
Quat rotation_x(float angle) {
    return {sin_f(angle * 0.5f), 0.0f, 0.0f, cos_f(angle * 0.5f)};
}

// JOML's rotationXYZ: turns about x, then y, then z.
Quat rotation_xyz(float ax, float ay, float az) {
    const float sx = sin_f(ax * 0.5f), cx = cos_f(ax * 0.5f);
    const float sy = sin_f(ay * 0.5f), cy = cos_f(ay * 0.5f);
    const float sz = sin_f(az * 0.5f), cz = cos_f(az * 0.5f);
    const float cycz = cy * cz, sysz = sy * sz, sycz = sy * cz, cysz = cy * sz;
    return {sx * cycz + cx * sysz, cx * sycz - sx * cysz, cx * cysz + sx * sycz, cx * cycz - sx * sysz};
}

// `Direction.getRotation`: what turns up into each face.
Quat face_rotation(int face) {
    constexpr float kPi = 3.1415927f;
    switch (face) {
        case 0: return rotation_x(kPi);
        case 1: return {};
        case 2: return rotation_xyz(kPi / 2.0f, 0.0f, kPi);
        case 3: return rotation_x(kPi / 2.0f);
        case 4: return rotation_xyz(kPi / 2.0f, 0.0f, kPi / 2.0f);
        default: return rotation_xyz(kPi / 2.0f, 0.0f, -kPi / 2.0f);
    }
}

// JOML's Quaternionf.transform: a vector turned by a quaternion.
void transform(const Quat& q, float v[3]) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, ww = q.w * q.w;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, xw = q.x * q.w;
    const float zw = q.z * q.w, yw = q.y * q.w, k = 1.0f / (xx + yy + zz + ww);
    const float x = v[0], y = v[1], z = v[2];
    v[0] = (xx - yy - zz + ww) * k * x + 2.0f * (xy - zw) * k * y + 2.0f * (xz + yw) * k * z;
    v[1] = 2.0f * (xy + zw) * k * x + (yy - xx - zz + ww) * k * y + 2.0f * (yz - xw) * k * z;
    v[2] = 2.0f * (xz - yw) * k * x + 2.0f * (yz + xw) * k * y + (zz - xx - yy + ww) * k * z;
}

} // namespace

BlockDestruction::Key BlockDestruction::key_of(const int32_t pos[3]) {
    return (static_cast<int64_t>(pos[0] & 0x3FFFFFF) << 38) | (static_cast<int64_t>(pos[2] & 0x3FFFFFF) << 12) |
           static_cast<int64_t>(pos[1] & 0xFFF);
}

// `LevelRenderer.removeProgress`: one breaker's crack taken off its block.
void BlockDestruction::remove(const Progress& p) {
    const auto found = by_block_.find(key_of(p.pos));
    if (found == by_block_.end()) return;
    found->second.erase({p.stage, p.id});
    if (found->second.empty()) by_block_.erase(found);
}

// `LevelRenderer.destroyBlockProgress`, `BlockDestructionProgress.setProgress`: a breaker's crack set or stopped.
void BlockDestruction::progress(int32_t id, const int32_t pos[3], int32_t stage) {
    if (stage >= 0 && stage < 10) {
        auto found = by_id_.find(id);
        if (found != by_id_.end()) remove(found->second);
        if (found == by_id_.end() || found->second.pos[0] != pos[0] || found->second.pos[1] != pos[1] ||
            found->second.pos[2] != pos[2]) {
            found = by_id_.insert_or_assign(id, Progress{id, {pos[0], pos[1], pos[2]}, 0, 0}).first;
        }
        Progress& p = found->second;
        p.stage = std::min(stage, 10);
        p.updated = ticks_;
        by_block_[key_of(p.pos)].insert({p.stage, p.id});
    } else {
        const auto found = by_id_.find(id);
        if (found == by_id_.end()) return;
        remove(found->second);
        by_id_.erase(found);
    }
}

// `LevelRenderer.tick`, `LevelRenderer.removeBlockBreakingProgress`: every 20 ticks, the stale cracks go.
void BlockDestruction::tick() {
    ++ticks_;
    if (ticks_ % 20 != 0) return;
    for (auto it = by_id_.begin(); it != by_id_.end();) {
        if (ticks_ - it->second.updated > 400) {
            remove(it->second);
            it = by_id_.erase(it);
        } else {
            ++it;
        }
    }
}

void BlockDestruction::clear() {
    by_id_.clear();
    by_block_.clear();
}

// `LevelRenderer.extractBlockDestroyAnimation`: the deepest crack of each block near the camera.
void BlockDestruction::shown(const double camera[3], std::vector<Shown>& out) const {
    out.clear();
    for (const auto& [key, set] : by_block_) {
        if (set.empty()) continue;
        const auto& p = by_id_.at(set.rbegin()->second);
        const double dx = p.pos[0] + 0.5 - camera[0];
        const double dy = p.pos[1] + 0.5 - camera[1];
        const double dz = p.pos[2] + 0.5 - camera[2];
        if (dx * dx + dy * dy + dz * dz > 1024.0) continue;
        out.push_back({{p.pos[0], p.pos[1], p.pos[2]}, set.rbegin()->first});
    }
}

// `BlockRenderDispatcher.renderBreakingTexture`, `SheetedDecalTextureGenerator.setNormal`: the block's faces, the crack projected on each.
void crack_quads(const BlockTable& table, int32_t state, const int32_t pos[3], const double offset[3],
                 const bool hidden[6], const double camera[3], uint32_t stage,
                 std::vector<CrackVertex>& out) {
    if ((table.flags(state) & BlockTable::kNoModel) != 0) return;
    const float origin[3] = {static_cast<float>(pos[0] - camera[0]), static_cast<float>(pos[1] - camera[1]),
                             static_cast<float>(pos[2] - camera[2])};
    for (const auto& quad : table.quads(state)) {
        const int cull = quad_cull_face(quad);
        if (cull >= 0 && hidden[cull]) continue;
        float corners[4][3];
        int face = 0;
        quad_corners(quad, corners, face);
        const Quat to_face = face_rotation(face);
        for (int c = 0; c < 4; ++c) {
            float local[3] = {corners[c][0] + static_cast<float>(offset[0]),
                              corners[c][1] + static_cast<float>(offset[1]),
                              corners[c][2] + static_cast<float>(offset[2])};
            CrackVertex v{};
            for (int a = 0; a < 3; ++a) v.position[a] = origin[a] + local[a];
            // Half a turn about y, a quarter back about x, as JOML rotates them.
            {
                const float s = sin_f(3.1415927f), k = cos_f(3.1415927f);
                const float x = local[0] * k + local[2] * s;
                const float z = -local[0] * s + local[2] * k;
                local[0] = x;
                local[2] = z;
            }
            {
                const float angle = -3.1415927f / 2.0f;
                const float s = sin_f(angle), k = cos_f(angle);
                const float y = local[1] * k - local[2] * s;
                const float z = local[1] * s + local[2] * k;
                local[1] = y;
                local[2] = z;
            }
            transform(to_face, local);
            v.uv[0] = -local[0];
            v.uv[1] = -local[1];
            v.stage = stage;
            out.push_back(v);
        }
    }
}

} // namespace mcpp::world
