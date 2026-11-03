#include "mcpp/session.hpp"
#include "session_detail.hpp"

#include "mcpp/nbt.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mcpp {
namespace {

// Velocity since 1.21.9. At rest it is one zero byte. Otherwise six bytes
// hold three fifteen bit components over -1 to 1 and two bits of scale, with
// a flag that more scale follows as a varint, which is how a fast arrow fits.
// Measured off the wire against known motions: 0.5 east, a quarter north,
// one up, and five east in seven bytes rather than six.
void read_velocity(ByteBuffer& in, double& x, double& y, double& z) {
    const uint8_t first = in.read_u8();
    if (first == 0) {
        x = y = z = 0.0;
        return;
    }
    const uint8_t second = in.read_u8();
    const auto rest = static_cast<uint32_t>(in.read_i32());
    const uint64_t packed =
        (static_cast<uint64_t>(rest) << 16) | (static_cast<uint64_t>(second) << 8) | first;
    uint64_t scale = packed & 3;
    if ((packed & 4) != 0) scale |= static_cast<uint64_t>(in.read_varint()) << 2;
    const auto component = [&](int shift) {
        const uint64_t bits = std::min<uint64_t>((packed >> shift) & 0x7FFF, 32766);
        return (static_cast<double>(bits) * 2.0 / 32766.0 - 1.0) * static_cast<double>(scale);
    };
    x = component(3);
    y = component(18);
    z = component(33);
}

// An angle is a 256th of a turn.
float read_angle(ByteBuffer& in) {
    return static_cast<float>(static_cast<int8_t>(in.read_u8())) * 360.0f / 256.0f;
}

} // namespace

std::string uuid_string(const uint64_t uuid[2]) {
    std::string out;
    size_t digits = 0;
    for (int half = 0; half < 2; ++half) {
        for (int shift = 60; shift >= 0; shift -= 4) {
            if (digits == 8 || digits == 12 || digits == 16 || digits == 20) out += '-';
            out += "0123456789abcdef"[(uuid[half] >> shift) & 0xF];
            ++digits;
        }
    }
    return out;
}

void Session::send_input_state(uint8_t keys) {
    ByteBuffer out;
    out.write_u8(keys);
    send(ids_->play_sb.player_input, out);
}

const std::map<int32_t, MobEffect>& Session::effects(int32_t entity) const {
    static const std::map<int32_t, MobEffect> none;
    const auto found = entities_.effects.find(entity);
    return found == entities_.effects.end() ? none : found->second;
}

const std::map<std::string, Attribute, std::less<>>& Session::attributes(int32_t entity) const {
    static const std::map<std::string, Attribute, std::less<>> none;
    const auto found = entities_.attributes.find(entity);
    return found == entities_.attributes.end() ? none : found->second;
}

double Session::attribute(int32_t entity, std::string_view name, double fallback) const {
    const auto& all = attributes(entity);
    const auto found = all.find(name);
    return found == all.end() ? fallback : found->second.value();
}

bool Session::handle_entities(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    if (packet.id == cb.add_entity) {
        Entity entity;
        entity.id = in.read_varint();
        entity.uuid[0] = static_cast<uint64_t>(in.read_i64());
        entity.uuid[1] = static_cast<uint64_t>(in.read_i64());
        entity.type = in.read_varint();
        entity.x = in.read_f64();
        entity.y = in.read_f64();
        entity.z = in.read_f64();
        // Velocity before the angles since 1.21.9, and pitch before yaw here,
        // the other way round from every movement packet after it.
        read_velocity(in, entity.vx, entity.vy, entity.vz);
        entity.pitch = read_angle(in);
        entity.yaw = read_angle(in);
        entity.head_yaw = read_angle(in);
        entity.data = in.read_varint();
        const auto& stored = entities_.entities[entity.id] = entity;
        entities_.move_bases[entity.id] = {entity.x, entity.y, entity.z};
        if (events_.entities.on_add) events_.entities.on_add(stored);
        return true;
    }
    if (packet.id == cb.entity_position_sync) {
        const int32_t id = in.read_varint();
        Entity& entity = entities_.entities[id];
        entity.id = id;
        entity.x = in.read_f64();
        entity.y = in.read_f64();
        entity.z = in.read_f64();
        entity.vx = in.read_f64();
        entity.vy = in.read_f64();
        entity.vz = in.read_f64();
        entity.yaw = in.read_f32();
        entity.pitch = in.read_f32();
        entity.on_ground = in.read_bool();
        entities_.move_bases[id] = {entity.x, entity.y, entity.z};
        if (events_.entities.on_move) events_.entities.on_move(entity);
        if (events_.entities.on_moved) {
            events_.entities.on_moved({id, entity.x, entity.y, entity.z, entity.yaw, entity.pitch,
                                       true, true, true});
        }
        return true;
    }
    if (packet.id == cb.move_entity_pos || packet.id == cb.move_entity_pos_rot ||
        packet.id == cb.move_entity_rot) {
        // Positions move in 1/4096 of a block from the last position sent,
        // rounded to that grid first; angles are absolute, yaw first. Every
        // one ends on the ground flag.
        const int32_t id = in.read_varint();
        int64_t delta[3] = {0, 0, 0};
        const bool moved = packet.id != cb.move_entity_rot;
        if (moved) {
            for (int64_t& d : delta) d = static_cast<int16_t>(in.read_u16());
        }
        float yaw = 0.0f;
        float pitch = 0.0f;
        const bool turned = packet.id != cb.move_entity_pos;
        if (turned) {
            yaw = read_angle(in);
            pitch = read_angle(in);
        }
        const bool on_ground = in.read_bool();
        const auto found = entities_.entities.find(id);
        if (found != entities_.entities.end()) {
            Entity& entity = found->second;
            auto& base = entities_.move_bases[id];
            if (moved) {
                for (int axis = 0; axis < 3; ++axis) {
                    if (delta[axis] == 0) continue;
                    const auto encoded = static_cast<int64_t>(std::floor(base[axis] * 4096.0 + 0.5));
                    base[axis] = static_cast<double>(encoded + delta[axis]) / 4096.0;
                }
                entity.x = base[0];
                entity.y = base[1];
                entity.z = base[2];
            }
            if (turned) {
                entity.yaw = yaw;
                entity.pitch = pitch;
            }
            entity.on_ground = on_ground;
            if (events_.entities.on_move) events_.entities.on_move(entity);
            if (events_.entities.on_moved) {
                events_.entities.on_moved({id, base[0], base[1], base[2], yaw, pitch, moved,
                                           turned, false});
            }
        }
        return true;
    }
    if (packet.id == cb.teleport_entity) {
        // Absolute unless a bit of the last int says that one field is an
        // offset: x, y, z, yaw, pitch, then the three velocities.
        const int32_t id = in.read_varint();
        double values[6];
        for (double& value : values) value = in.read_f64();
        const float yaw = in.read_f32();
        const float pitch = in.read_f32();
        const auto relative = static_cast<uint32_t>(in.read_i32());
        const bool on_ground = in.read_bool();
        const auto found = entities_.entities.find(id);
        if (found != entities_.entities.end()) {
            Entity& entity = found->second;
            double* fields[6] = {&entity.x, &entity.y, &entity.z, &entity.vx, &entity.vy,
                                 &entity.vz};
            const int bits[6] = {0, 1, 2, 5, 6, 7};
            for (int i = 0; i < 6; ++i) {
                *fields[i] = (relative & (1u << bits[i])) != 0 ? *fields[i] + values[i] : values[i];
            }
            entity.yaw = (relative & (1u << 3)) != 0 ? entity.yaw + yaw : yaw;
            entity.pitch = (relative & (1u << 4)) != 0 ? entity.pitch + pitch : pitch;
            entity.on_ground = on_ground;
            if (events_.entities.on_move) events_.entities.on_move(entity);
            if (events_.entities.on_moved) {
                events_.entities.on_moved({id, entity.x, entity.y, entity.z, entity.yaw,
                                           entity.pitch, true, true, true});
            }
        }
        return true;
    }
    if (packet.id == cb.rotate_head) {
        const int32_t id = in.read_varint();
        const float head_yaw = read_angle(in);
        if (const auto found = entities_.entities.find(id); found != entities_.entities.end()) {
            found->second.head_yaw = head_yaw;
            if (events_.entities.on_move) events_.entities.on_move(found->second);
            if (events_.entities.on_head) events_.entities.on_head(id, head_yaw);
        }
        return true;
    }
    if (packet.id == cb.set_entity_motion) {
        const auto before = in.position();
        const int32_t id = in.read_varint();
        // Our own knockback belongs to the player area.
        if (id == entity_id_) {
            in.set_position(before);
            return false;
        }
        double vx = 0.0;
        double vy = 0.0;
        double vz = 0.0;
        read_velocity(in, vx, vy, vz);
        if (const auto found = entities_.entities.find(id); found != entities_.entities.end()) {
            found->second.vx = vx;
            found->second.vy = vy;
            found->second.vz = vz;
        }
        return true;
    }
    if (packet.id == cb.entity_event) {
        const int32_t id = in.read_i32();
        const uint8_t status = in.read_u8();
        if (events_.entities.on_event) events_.entities.on_event(id, status);
        return true;
    }
    if (packet.id == cb.animate) {
        const int32_t id = in.read_varint();
        const uint8_t action = in.read_u8();
        if (events_.entities.on_animate) events_.entities.on_animate(id, action);
        return true;
    }
    if (packet.id == cb.hurt_animation) {
        const int32_t id = in.read_varint();
        const float yaw = in.read_f32();
        if (events_.entities.on_hurt) events_.entities.on_hurt(id, yaw);
        return true;
    }
    if (packet.id == cb.damage_event) {
        // The type, the entity behind it and the one that touched, each id
        // shifted up by one so zero can mean none, and where it came from.
        const int32_t id = in.read_varint();
        in.read_varint();
        in.read_varint();
        in.read_varint();
        if (in.read_bool()) {
            in.read_f64();
            in.read_f64();
            in.read_f64();
        }
        if (events_.entities.on_damage) events_.entities.on_damage(id);
        return true;
    }
    if (packet.id == cb.set_passengers) {
        const int32_t vehicle = in.read_varint();
        const int32_t count = in.read_varint();
        if (count < 0 || count > 4096) throw ProtocolError("implausible passenger count");
        std::vector<int32_t> riders;
        for (int32_t i = 0; i < count; ++i) riders.push_back(in.read_varint());
        const auto found = entities_.entities.find(vehicle);
        if (found == entities_.entities.end()) return true;
        for (const int32_t old_rider : found->second.passengers) {
            if (const auto rider = entities_.entities.find(old_rider);
                rider != entities_.entities.end() && rider->second.vehicle == vehicle) {
                rider->second.vehicle = -1;
            }
        }
        found->second.passengers = std::move(riders);
        for (const int32_t rider_id : found->second.passengers) {
            if (const auto rider = entities_.entities.find(rider_id);
                rider != entities_.entities.end()) {
                rider->second.vehicle = vehicle;
            }
        }
        if (events_.entities.on_passengers) events_.entities.on_passengers(found->second);
        return true;
    }
    if (packet.id == cb.set_entity_link) {
        const int32_t attached = in.read_i32();
        const int32_t holder = in.read_i32();
        if (const auto found = entities_.entities.find(attached);
            found != entities_.entities.end()) {
            found->second.leashed_to = holder == 0 ? -1 : holder;
        }
        return true;
    }
    if (packet.id == cb.player_info_remove) {
        const int32_t count = in.read_varint();
        if (count < 0 || count > 4096) throw ProtocolError("implausible player list size");
        for (int32_t i = 0; i < count; ++i) {
            const auto uuid = detail::read_uuid(in);
            entities_.players.erase(uuid);
            if (events_.entities.on_player_remove) events_.entities.on_player_remove(uuid);
        }
        return true;
    }
    if (packet.id == cb.remove_entities) {
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int32_t id = in.read_varint();
            entities_.entities.erase(id);
            entities_.move_bases.erase(id);
            entities_.effects.erase(id);
            entities_.attributes.erase(id);
            if (events_.entities.on_remove) events_.entities.on_remove(id);
        }
        return true;
    }
    if (packet.id == cb.bundle_delimiter) {
        entities_.in_bundle = !entities_.in_bundle;
        return true;
    }
    if (packet.id == cb.update_mob_effect) {
        const int32_t entity = in.read_varint();
        MobEffect effect;
        effect.id = in.read_varint();
        effect.amplifier = in.read_varint();
        effect.duration = in.read_varint();
        const uint8_t flags = in.read_u8();
        effect.ambient = (flags & 0x01) != 0;
        effect.particles = (flags & 0x02) != 0;
        effect.icon = (flags & 0x04) != 0;
        effect.blend = (flags & 0x08) != 0;
        entities_.effects[entity][effect.id] = effect;
        return true;
    }
    if (packet.id == cb.remove_mob_effect) {
        const int32_t entity = in.read_varint();
        const int32_t id = in.read_varint();
        if (const auto found = entities_.effects.find(entity); found != entities_.effects.end()) {
            found->second.erase(id);
        }
        return true;
    }
    if (packet.id == cb.update_attributes) {
        // Each attribute arrives whole, modifiers and all, so it replaces
        // the one before it; attributes it does not name are left alone.
        const int32_t entity = in.read_varint();
        auto& attributes = entities_.attributes[entity];
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int32_t id = in.read_varint();
            const auto* entry = data_.registries.at("minecraft:attribute", static_cast<size_t>(id));
            Attribute attribute;
            attribute.base = in.read_f64();
            const int32_t modifiers = in.read_varint();
            for (int32_t m = 0; m < modifiers; ++m) {
                Attribute::Modifier modifier;
                modifier.id = in.read_string();
                modifier.amount = in.read_f64();
                modifier.operation = in.read_u8();
                attribute.modifiers.push_back(std::move(modifier));
            }
            attributes[entry != nullptr ? entry->name : std::format("#{}", id)] =
                std::move(attribute);
        }
        return true;
    }
    if (packet.id == cb.player_info_update) {
        read_player_list(in);
        return true;
    }
    return false;
}

// Every line is kept whole: the name and skin are what a player entity is drawn
// with, and the latency is the server's own measure of ours.
void Session::read_player_list(ByteBuffer& in) {
    if (entities_.player_list_broken) return;

    enum Action : uint8_t {
        kAddPlayer, kInitialiseChat, kGameMode, kListed, kLatency, kDisplayName,
        kListPriority, kHat, kActionCount
    };

    try {
        const auto actions = in.read_u8();
        const int32_t count = in.read_varint();
        if (count < 0 || count > 4096) throw ProtocolError("implausible player list size");

        const auto selected = [&](Action action) {
            return (actions & (1u << static_cast<uint8_t>(action))) != 0;
        };

        std::vector<std::string> changed;
        for (int32_t i = 0; i < count; ++i) {
            const auto uuid = detail::read_uuid(in);
            PlayerInfo& info = entities_.players[uuid];
            info.uuid = uuid;
            for (uint8_t action = 0; action < kActionCount; ++action) {
                if (!selected(static_cast<Action>(action))) continue;
                switch (action) {
                case kAddPlayer: {
                    info.name = in.read_string(16);
                    const int32_t properties = in.read_varint();
                    if (properties < 0 || properties > 64) {
                        throw ProtocolError("implausible property count");
                    }
                    info.properties.clear();
                    for (int32_t p = 0; p < properties; ++p) {
                        PlayerInfo::Property property;
                        property.name = in.read_string(64);
                        property.value = in.read_string(32767);
                        if (in.read_bool()) property.signature = in.read_string(1024);
                        info.properties.push_back(std::move(property));
                    }
                    break;
                }
                case kInitialiseChat:
                    info.chat_session = in.read_bool();
                    if (info.chat_session) {
                        in.read_bytes(detail::kUuidBytes);
                        in.read_i64();
                        in.read_bytes(static_cast<size_t>(in.read_varint()));
                        in.read_bytes(static_cast<size_t>(in.read_varint()));
                    }
                    break;
                case kGameMode:
                    info.game_mode = in.read_varint();
                    break;
                case kListed:
                    info.listed = in.read_bool();
                    break;
                case kLatency:
                    info.latency = in.read_varint();
                    if (uuid == profile_uuid_) entities_.ping_ms = info.latency;
                    break;
                case kDisplayName:
                    info.display_name =
                        in.read_bool() ? detail::read_component(in, lookup_) : std::string();
                    break;
                case kListPriority:
                    info.list_priority = in.read_varint();
                    break;
                case kHat:
                    info.show_hat = in.read_bool();
                    break;
                default:
                    break;
                }
            }
            changed.push_back(uuid);
        }

        // The packet has to end here. Anything left means the action list grew
        // and everything after this point was read as the wrong field.
        if (in.remaining() != 0) throw ProtocolError("trailing bytes");
        if (events_.entities.on_player_info) {
            for (const auto& uuid : changed) events_.entities.on_player_info(entities_.players[uuid]);
        }
    } catch (const std::exception& error) {
        entities_.player_list_broken = true;
        entities_.ping_ms = -1;
        status(std::format("player list not understood ({}); no ping from the server",
                           error.what()));
    }
}

} // namespace mcpp
