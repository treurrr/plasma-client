#include "mcpp/session.hpp"
#include "session_detail.hpp"

#include "mcpp/nbt.hpp"

#include <algorithm>
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
        if (events_.entities.on_move) events_.entities.on_move(entity);
        return true;
    }
    if (packet.id == cb.move_entity_pos || packet.id == cb.move_entity_pos_rot ||
        packet.id == cb.move_entity_rot) {
        // Positions move in 1/4096 of a block relative to where it last was;
        // angles are absolute, yaw first. Every one ends on the ground flag.
        const int32_t id = in.read_varint();
        double dx = 0.0;
        double dy = 0.0;
        double dz = 0.0;
        if (packet.id != cb.move_entity_rot) {
            dx = static_cast<double>(static_cast<int16_t>(in.read_u16())) / 4096.0;
            dy = static_cast<double>(static_cast<int16_t>(in.read_u16())) / 4096.0;
            dz = static_cast<double>(static_cast<int16_t>(in.read_u16())) / 4096.0;
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
            entity.x += dx;
            entity.y += dy;
            entity.z += dz;
            if (turned) {
                entity.yaw = yaw;
                entity.pitch = pitch;
            }
            entity.on_ground = on_ground;
            if (events_.entities.on_move) events_.entities.on_move(entity);
        }
        return true;
    }
    if (packet.id == cb.remove_entities) {
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int32_t id = in.read_varint();
            entities_.entities.erase(id);
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

// The game measures latency itself and publishes it in the player list, so the
// number the hud shows is the server's own, not something we estimate.
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

        for (int32_t i = 0; i < count; ++i) {
            const auto uuid = detail::read_uuid(in);
            int32_t latency = -1;
            for (uint8_t action = 0; action < kActionCount; ++action) {
                if (!selected(static_cast<Action>(action))) continue;
                switch (action) {
                case kAddPlayer: {
                    in.read_string();
                    const int32_t properties = in.read_varint();
                    if (properties < 0 || properties > 64) {
                        throw ProtocolError("implausible property count");
                    }
                    for (int32_t p = 0; p < properties; ++p) {
                        in.read_string();
                        in.read_string(32767);
                        if (in.read_bool()) in.read_string(32767);
                    }
                    break;
                }
                case kInitialiseChat:
                    if (in.read_bool()) {
                        in.read_bytes(detail::kUuidBytes);
                        in.read_i64();
                        in.read_bytes(static_cast<size_t>(in.read_varint()));
                        in.read_bytes(static_cast<size_t>(in.read_varint()));
                    }
                    break;
                case kGameMode:
                case kListPriority:
                    in.read_varint();
                    break;
                case kListed:
                case kHat:
                    in.read_bool();
                    break;
                case kLatency:
                    latency = in.read_varint();
                    break;
                case kDisplayName:
                    if (in.read_bool()) nbt::read_network_tag(in);
                    break;
                default:
                    break;
                }
            }
            if (latency >= 0 && uuid == profile_uuid_) entities_.ping_ms = latency;
        }

        // The packet has to end here. Anything left means the action list grew
        // and everything after this point was read as the wrong field.
        if (in.remaining() != 0) throw ProtocolError("trailing bytes");
    } catch (const std::exception& error) {
        entities_.player_list_broken = true;
        entities_.ping_ms = -1;
        status(std::format("player list not understood ({}); no ping from the server",
                           error.what()));
    }
}

} // namespace mcpp
