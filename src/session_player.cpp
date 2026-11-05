#include "mcpp/session.hpp"

#include <algorithm>
#include <format>

namespace mcpp {
namespace {

// One stack off the wire. A component whose layout is wrong or missing loses
// the rest of the packet with it: `readable` and `problem` say so.
ItemStack read_slot(ByteBuffer& in, int32_t protocol, bool& readable, std::string& problem) {
    try {
        return proto::read_item_stack(in, protocol);
    } catch (const ProtocolError& error) {
        readable = false;
        problem = error.what();
        ItemStack stack;
        stack.readable = false;
        return stack;
    }
}

// The game's packed velocity: three 15-bit components and a scale.
void read_lp_vec3(ByteBuffer& in, double& x, double& y, double& z) {
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

int64_t pack_position(int32_t x, int32_t y, int32_t z) {
    return ((static_cast<int64_t>(x) & 0x3FFFFFF) << 38) |
           ((static_cast<int64_t>(z) & 0x3FFFFFF) << 12) |
           (static_cast<int64_t>(y) & 0xFFF);
}

} // namespace

bool Session::handle_player(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    if (packet.id == cb.player_position) {
        const int32_t teleport = in.read_varint();
        const double x = in.read_f64();
        const double y = in.read_f64();
        const double z = in.read_f64();
        const double vx = in.read_f64();
        const double vy = in.read_f64();
        const double vz = in.read_f64();
        const float yaw = in.read_f32();
        const float pitch = in.read_f32();
        // Each bit makes one field an offset from where the player already is
        // rather than a place: x, y and z, then yaw and pitch. Measured: a
        // plain teleport sends 0x18, keeping the player's own facing, and a
        // server correcting a few blocks sends 0xff with the offsets alone -
        // read as a place, that drops the player under the world at 0, 0.
        const auto relative = static_cast<uint32_t>(in.read_i32());
        if (events_.player.on_teleport) {
            SessionEvents::Player::Teleport t;
            t.id = teleport;
            t.x = x;
            t.y = y;
            t.z = z;
            t.vx = vx;
            t.vy = vy;
            t.vz = vz;
            t.yaw = yaw;
            t.pitch = pitch;
            t.relative = relative;
            events_.player.on_teleport(t);
            return true;
        }
        const auto pick = [&](uint32_t bit, double value, double was) {
            return (relative & bit) != 0 ? was + value : value;
        };
        player_.last_x = pick(0x01, x, player_.last_x);
        player_.last_y = pick(0x02, y, player_.last_y);
        player_.last_z = pick(0x04, z, player_.last_z);
        player_.last_yaw = static_cast<float>(pick(0x08, yaw, player_.last_yaw));
        player_.last_pitch = static_cast<float>(pick(0x10, pitch, player_.last_pitch));
        if (events_.player.on_position) {
            events_.player.on_position(player_.last_x, player_.last_y, player_.last_z,
                                       player_.last_yaw, player_.last_pitch);
        }
        accept_teleport(teleport, player_.last_x, player_.last_y, player_.last_z,
                        player_.last_yaw, player_.last_pitch);
        return true;
    }
    if (packet.id == cb.set_entity_motion && events_.player.on_motion) {
        const auto before = in.position();
        const int32_t id = in.read_varint();
        if (id != entity_id_) {
            in.set_position(before);
            return false;
        }
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        read_lp_vec3(in, x, y, z);
        events_.player.on_motion(x, y, z);
        return true;
    }
    if (packet.id == cb.player_abilities) {
        // Measured per game mode: survival 0x00, creative 0x0d, spectator 0x07.
        const uint8_t flags = in.read_u8();
        player_.abilities.invulnerable = (flags & 0x01) != 0;
        player_.abilities.flying = (flags & 0x02) != 0;
        player_.abilities.may_fly = (flags & 0x04) != 0;
        player_.abilities.instant_build = (flags & 0x08) != 0;
        player_.abilities.flying_speed = in.read_f32();
        player_.abilities.walking_speed = in.read_f32();
        return true;
    }
    if (packet.id == cb.set_health) {
        player_.vitals.health = in.read_f32();
        player_.vitals.food = in.read_varint();
        player_.vitals.saturation = in.read_f32();
        // The game shows a death screen and waits for the player to press
        // respawn. Until there is one, a dead player would stay dead forever:
        // invisible to everyone, selected by nothing, on zero hearts.
        if (player_.vitals.health <= 0.0f) {
            ByteBuffer out;
            out.write_varint(0);   // perform respawn
            send(ids_->play_sb.client_command, out);
            status("died; asked the server to respawn");
        }
        return true;
    }
    if (packet.id == cb.set_experience) {
        player_.vitals.experience = in.read_f32();
        player_.vitals.level = in.read_varint();
        player_.vitals.total_experience = in.read_varint();
        return true;
    }
    if (packet.id == cb.set_held_slot) {
        const int32_t slot = in.read_varint();
        if (slot >= 0 && slot < static_cast<int32_t>(Inventory::kHotbarSlots)) {
            player_.inventory.held = slot;
        }
        return true;
    }
    if (packet.id == cb.container_set_content) {
        const int32_t container = in.read_varint();
        const int32_t state = in.read_varint();
        if (container == 0) player_.container_state = state;
        const int32_t count = in.read_varint();
        if (count < 0) return true;
        // Every container is read whole; only our own inventory is kept for now.
        const bool ours = container == 0;
        bool readable = true;
        std::string problem;
        for (int32_t i = 0; i < count && readable; ++i) {
            const ItemStack stack = read_slot(in, options_.protocol, readable, problem);
            if (ours && i < static_cast<int32_t>(Inventory::kSlots)) {
                player_.inventory.slots[i] = stack;
            }
        }
        if (readable) {
            const ItemStack carried = read_slot(in, options_.protocol, readable, problem);
            if (ours) player_.inventory.carried = carried;
        }
        if (!readable && !player_.slot_format_warned) {
            player_.slot_format_warned = true;
            status(std::format("the slots after this one are not read: {}", problem));
        }
        return true;
    }
    if (packet.id == cb.container_set_slot) {
        const int32_t container = in.read_varint();
        const int32_t state = in.read_varint();
        if (container == 0) player_.container_state = state;
        const auto slot = static_cast<int16_t>(in.read_u16());
        bool readable = true;
        std::string problem;
        const ItemStack stack = read_slot(in, options_.protocol, readable, problem);
        if (!readable && !player_.slot_format_warned) {
            player_.slot_format_warned = true;
            status(std::format("a slot is not read: {}", problem));
        }
        if (container == 0 && slot >= 0 && slot < static_cast<int32_t>(Inventory::kSlots)) {
            player_.inventory.slots[slot] = stack;
        }
        return true;
    }
    if (packet.id == cb.block_changed_ack) {
        const int32_t sequence = in.read_varint();
        if (events_.player.on_block_changed_ack) events_.player.on_block_changed_ack(sequence);
        return true;
    }
    if (packet.id == cb.block_destruction) {
        const int32_t id = in.read_varint();
        const int64_t packed = in.read_i64();
        const auto x = static_cast<int32_t>(packed >> 38);
        const auto y = static_cast<int32_t>(packed << 52 >> 52);
        const auto z = static_cast<int32_t>(packed << 26 >> 38);
        const int32_t stage = in.read_u8();
        if (events_.player.on_block_destruction) events_.player.on_block_destruction(id, x, y, z, stage);
        return true;
    }
    return false;
}

void Session::send_position(double x, double y, double z, float yaw, float pitch,
                            bool on_ground) {
    if (state_ != SessionState::Play) return;
    player_.last_x = x;
    player_.last_y = y;
    player_.last_z = z;
    player_.last_yaw = yaw;
    player_.last_pitch = pitch;

    ByteBuffer out;
    out.write_f64(x);
    out.write_f64(y);
    out.write_f64(z);
    out.write_f32(yaw);
    out.write_f32(pitch);
    // One flags byte since 1.21.4: bit 0 on ground, bit 1 horizontal collision.
    out.write_u8(on_ground ? 1 : 0);
    send(ids_->play_sb.move_player_pos_rot, out);
    send(ids_->play_sb.client_tick_end, ByteBuffer{});
}

void Session::accept_teleport(int32_t id, double x, double y, double z, float yaw, float pitch) {
    player_.last_x = x;
    player_.last_y = y;
    player_.last_z = z;
    player_.last_yaw = yaw;
    player_.last_pitch = pitch;
    if (player_.announced_loaded) ++player_.teleports;
    ByteBuffer out;
    out.write_varint(id);
    send(ids_->play_sb.accept_teleportation, out);
    // The game answers a teleport with where it now stands, straight away,
    // neither on the ground nor against a wall. What it last sent stays as
    // it was, so the next tick reports the new place again.
    ByteBuffer here;
    here.write_f64(x);
    here.write_f64(y);
    here.write_f64(z);
    here.write_f32(yaw);
    here.write_f32(pitch);
    here.write_u8(0);
    send(ids_->play_sb.move_player_pos_rot, here);
    // Since 1.21.2 the server keeps the player hidden from everyone else
    // until the client says it has finished loading. Without this you are
    // in the world, you can walk and chat, and nobody can see you.
    if (!player_.announced_loaded) {
        player_.announced_loaded = true;
        send(ids_->play_sb.player_loaded, ByteBuffer{});
        status("told the server we are loaded");
    }
}

void Session::send_movement(const Movement& m) {
    if (state_ != SessionState::Play) return;
    auto& p = player_;
    if (m.keys != p.sent_keys) {
        ByteBuffer keys;
        keys.write_u8(m.keys);
        send(ids_->play_sb.player_input, keys);
        p.sent_keys = m.keys;
    }
    if (m.sprinting != p.sent_sprinting) {
        ByteBuffer command;
        command.write_varint(entity_id());
        command.write_varint(m.sprinting ? 1 : 2);   // start or stop sprinting
        command.write_varint(0);
        send(ids_->play_sb.player_command, command);
        p.sent_sprinting = m.sprinting;
    }
    const double dx = m.x - p.sent_x;
    const double dy = m.y - p.sent_y;
    const double dz = m.z - p.sent_z;
    const double dyaw = static_cast<double>(m.yaw) - static_cast<double>(p.sent_yaw);
    const double dpitch = static_cast<double>(m.pitch) - static_cast<double>(p.sent_pitch);
    ++p.position_reminder;
    const bool moved = dx * dx + dy * dy + dz * dz > 2.0e-4 * 2.0e-4 || p.position_reminder >= 20;
    const bool turned = dyaw != 0.0 || dpitch != 0.0;
    const uint8_t flags = static_cast<uint8_t>((m.on_ground ? 1 : 0) | (m.horizontal_collision ? 2 : 0));
    ByteBuffer out;
    if (moved) {
        out.write_f64(m.x);
        out.write_f64(m.y);
        out.write_f64(m.z);
    }
    if (turned) {
        out.write_f32(m.yaw);
        out.write_f32(m.pitch);
    }
    if (moved || turned || m.on_ground != p.sent_on_ground ||
        m.horizontal_collision != p.sent_collision) {
        out.write_u8(flags);
        const auto id = moved && turned ? ids_->play_sb.move_player_pos_rot
                        : moved          ? ids_->play_sb.move_player_pos
                        : turned         ? ids_->play_sb.move_player_rot
                                         : ids_->play_sb.move_player_status_only;
        send(id, out);
    }
    if (moved) {
        p.sent_x = m.x;
        p.sent_y = m.y;
        p.sent_z = m.z;
        p.position_reminder = 0;
    }
    if (turned) {
        p.sent_yaw = m.yaw;
        p.sent_pitch = m.pitch;
    }
    p.sent_on_ground = m.on_ground;
    p.sent_collision = m.horizontal_collision;
    p.last_x = m.x;
    p.last_y = m.y;
    p.last_z = m.z;
    p.last_yaw = m.yaw;
    p.last_pitch = m.pitch;
}

void Session::send_tick_end() {
    if (state_ != SessionState::Play) return;
    send(ids_->play_sb.client_tick_end, ByteBuffer{});
}

void Session::send_flying(bool flying) {
    ByteBuffer out;
    out.write_u8(flying ? 0x02 : 0x00);
    send(ids_->play_sb.player_abilities, out);
    player_.abilities.flying = flying;
}

int32_t Session::effect_level(std::string_view name) const {
    for (const auto& [id, effect] : effects(entity_id())) {
        const auto* entry = registries().at("minecraft:mob_effect", id);
        if (entry != nullptr && entry->name == name) return effect.amplifier + 1;
    }
    return 0;
}

void Session::send_held_slot(int32_t slot) {
    if (slot < 0 || slot >= static_cast<int32_t>(Inventory::kHotbarSlots)) return;
    if (slot == player_.inventory.held) return;
    player_.inventory.held = slot;
    ByteBuffer out;
    out.write_u16(static_cast<uint16_t>(slot));
    send(ids_->play_sb.set_carried_item, out);
}

void Session::send_dig(int32_t x, int32_t y, int32_t z, int32_t face, bool finished) {
    ByteBuffer out;
    out.write_varint(finished ? 2 : 0);   // finished digging, or started
    out.write_i64(pack_position(x, y, z));
    out.write_u8(static_cast<uint8_t>(face));
    out.write_varint(++player_.sequence);
    send(ids_->play_sb.player_action, out);
}

void Session::send_dig_action(int32_t action, int32_t x, int32_t y, int32_t z, int32_t face) {
    ByteBuffer out;
    out.write_varint(action);
    out.write_i64(pack_position(x, y, z));
    out.write_u8(static_cast<uint8_t>(face));
    out.write_varint(action == 1 ? 0 : ++player_.sequence);
    send(ids_->play_sb.player_action, out);
}

void Session::send_place(int32_t x, int32_t y, int32_t z, int32_t face, float cursor_x,
                         float cursor_y, float cursor_z) {
    ByteBuffer out;
    out.write_varint(0);   // the main hand
    out.write_i64(pack_position(x, y, z));
    out.write_varint(face);
    out.write_f32(cursor_x);
    out.write_f32(cursor_y);
    out.write_f32(cursor_z);
    out.write_bool(false);   // not standing inside the block
    out.write_bool(false);   // the world border was not in the way
    out.write_varint(++player_.sequence);
    send(ids_->play_sb.use_item_on, out);
}

void Session::send_swing() {
    ByteBuffer out;
    out.write_varint(0);   // the main hand
    send(ids_->play_sb.swing, out);
}

} // namespace mcpp
