#include "mcpp/session.hpp"

namespace mcpp {
namespace {

// Zero means the event follows inline, as a name and maybe a fixed range;
// anything else is one more than its id in the sound_event registry.
void read_sound_event(ByteBuffer& in, const Registries& registries,
                      SessionEvents::Sound::Cue& cue) {
    const int32_t holder = in.read_varint();
    if (holder == 0) {
        cue.event = in.read_string();
        cue.has_fixed_range = in.read_bool();
        if (cue.has_fixed_range) cue.fixed_range = in.read_f32();
        return;
    }
    const auto* entry = registries.at("minecraft:sound_event", static_cast<size_t>(holder - 1));
    cue.event = entry != nullptr ? entry->name : std::string();
}

// `BlockPos.getX`, `BlockPos.getY`, `BlockPos.getZ`: a packed position's three fields.
void unpack_position(int64_t packed, int32_t& x, int32_t& y, int32_t& z) {
    const auto extend = [](int64_t value, int bits) {
        const int64_t sign = int64_t{1} << (bits - 1);
        return static_cast<int32_t>((value ^ sign) - sign);
    };
    x = extend((packed >> 38) & 0x3FFFFFF, 26);
    z = extend((packed >> 12) & 0x3FFFFFF, 26);
    y = extend(packed & 0xFFF, 12);
}

} // namespace

bool Session::handle_sound(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;
    auto& events = events_.sound;

    if (packet.id == cb.sound || packet.id == cb.sound_entity) {
        SessionEvents::Sound::Cue cue;
        read_sound_event(in, data_.registries, cue);
        cue.category = in.read_varint();
        if (packet.id == cb.sound) {
            // An eighth of a block, so the sound sits where the server said
            // to within the grid it was rounded to.
            cue.x = in.read_i32() / 8.0;
            cue.y = in.read_i32() / 8.0;
            cue.z = in.read_i32() / 8.0;
        } else {
            cue.entity = in.read_varint();
        }
        cue.volume = in.read_f32();
        cue.pitch = in.read_f32();
        cue.seed = in.read_i64();
        if (events.on_sound) events.on_sound(cue);
        return true;
    }
    if (packet.id == cb.stop_sound) {
        const uint8_t flags = in.read_u8();
        const int32_t category = (flags & 1) != 0 ? in.read_varint() : -1;
        const std::string name = (flags & 2) != 0 ? in.read_string() : std::string();
        if (events.on_stop) events.on_stop(category, name);
        return true;
    }
    if (packet.id == cb.level_event) {
        const int32_t type = in.read_i32();
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        unpack_position(in.read_i64(), x, y, z);
        const int32_t data = in.read_i32();
        const bool global = in.read_bool();
        if (events.on_level_event) events.on_level_event(type, x, y, z, data, global);
        return true;
    }
    return false;
}

} // namespace mcpp
