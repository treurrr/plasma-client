#include "mcpp/session.hpp"

#include <format>

namespace mcpp {
namespace {

// One stack off the wire. Data components carry a body whose shape depends on
// the component type, and those shapes are not in the reports, so a stack that
// has any is read as far as its id and count and then gives up: `readable`
// tells the caller that everything after this point in the packet is lost.
ItemStack read_slot(ByteBuffer& in, bool& readable) {
    ItemStack stack;
    const int32_t count = in.read_varint();
    if (count <= 0) return stack;
    stack.count = count;
    stack.id = in.read_varint();
    const int32_t added = in.read_varint();
    const int32_t removed = in.read_varint();
    if (added != 0) {
        stack.readable = false;
        readable = false;
        return stack;
    }
    for (int32_t i = 0; i < removed; ++i) in.read_varint();
    return stack;
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
        in.read_f64();   // velocity, which this client works out itself
        in.read_f64();
        in.read_f64();
        const float yaw = in.read_f32();
        const float pitch = in.read_f32();
        // Each bit makes one field an offset from where the player already is
        // rather than a place: x, y and z, then yaw and pitch. Measured: a
        // plain teleport sends 0x18, keeping the player's own facing, and a
        // server correcting a few blocks sends 0xff with the offsets alone -
        // read as a place, that drops the player under the world at 0, 0.
        const auto relative = static_cast<uint32_t>(in.read_i32());
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
        ByteBuffer out;
        out.write_varint(teleport);
        send(ids_->play_sb.accept_teleportation, out);
        // Since 1.21.2 the server keeps the player hidden from everyone else
        // until the client says it has finished loading. Without this you are
        // in the world, you can walk and chat, and nobody can see you.
        if (!player_.announced_loaded) {
            player_.announced_loaded = true;
            ByteBuffer loaded;
            send(ids_->play_sb.player_loaded, loaded);
            status("told the server we are loaded");
        }
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
        in.read_varint();   // state id, only needed to echo back on a click
        const int32_t count = in.read_varint();
        if (container != 0 || count < 0) return true;   // only our own inventory for now
        bool readable = true;
        for (int32_t i = 0; i < count && readable; ++i) {
            const ItemStack stack = read_slot(in, readable);
            if (i < static_cast<int32_t>(Inventory::kSlots)) player_.inventory.slots[i] = stack;
        }
        if (readable) player_.inventory.carried = read_slot(in, readable);
        if (!readable && !player_.slot_format_warned) {
            player_.slot_format_warned = true;
            status("an item carries data components; the slots after it are not read");
        }
        return true;
    }
    if (packet.id == cb.container_set_slot) {
        const int32_t container = in.read_varint();
        in.read_varint();   // state id
        const auto slot = static_cast<int16_t>(in.read_u16());
        bool readable = true;
        const ItemStack stack = read_slot(in, readable);
        if (container == 0 && slot >= 0 && slot < static_cast<int32_t>(Inventory::kSlots)) {
            player_.inventory.slots[slot] = stack;
        }
        return true;
    }
    if (packet.id == cb.block_changed_ack) {
        in.read_varint();   // the sequence we sent; nothing to undo yet
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

    send(ids_->play_sb.client_tick_end, ByteBuffer{});

    ByteBuffer out;
    out.write_f64(x);
    out.write_f64(y);
    out.write_f64(z);
    out.write_f32(yaw);
    out.write_f32(pitch);
    // One flags byte since 1.21.4: bit 0 on ground, bit 1 horizontal collision.
    out.write_u8(on_ground ? 1 : 0);
    send(ids_->play_sb.move_player_pos_rot, out);
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
