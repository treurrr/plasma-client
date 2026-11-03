#include "mcpp/session.hpp"

#include "mcpp/world/chunk.hpp"

#include <format>
#include <fstream>

namespace mcpp {
namespace {

// A block position is one long: 26 bits of x, 26 of z and 12 of y, all signed,
// which is why each one is sign extended on the way out.
void unpack_position(int64_t packed, int32_t& x, int32_t& y, int32_t& z) {
    const auto extend = [](int64_t value, int bits) {
        const int64_t sign = int64_t{1} << (bits - 1);
        return static_cast<int32_t>((value ^ sign) - sign);
    };
    x = extend((packed >> 38) & 0x3FFFFFF, 26);
    z = extend((packed >> 12) & 0x3FFFFFF, 26);
    y = extend(packed & 0xFFF, 12);
}

constexpr float kChunksPerTick = 32.0f;    // the server throttles to what we claim

} // namespace

// What login and respawn both carry: which dimension, its type, the game mode
// and a few world facts. The type is an index into the dimension_type registry
// sent in configuration, and that entry is where the world's height lives.
void Session::read_spawn_info(ByteBuffer& in) {
    const int32_t type = in.read_varint();
    world_.dimension.name = in.read_string();
    in.read_i64();   // hashed seed
    player_.vitals.game_mode = static_cast<int8_t>(in.read_u8());
    in.read_u8();    // previous game mode
    in.read_bool();  // debug world
    in.read_bool();  // flat world
    if (in.read_bool()) {   // where the player last died
        in.read_string();
        in.read_i64();
    }
    in.read_varint();   // portal cooldown
    world_.dimension.sea_level = in.read_varint();

    if (const auto* entry = data_.registries.at("minecraft:dimension_type",
                                                static_cast<size_t>(type))) {
        world_.dimension.type = entry->name;
        if (entry->data.has_value()) {
            const auto& tag = *entry->data;
            const auto integer = [&](std::string_view key, int32_t fallback) {
                const auto* value = tag.find(key);
                return value != nullptr ? static_cast<int32_t>(value->as_int().value_or(fallback))
                                        : fallback;
            };
            const auto flag = [&](std::string_view key, bool fallback) {
                const auto* value = tag.find(key);
                return value != nullptr ? value->as_int().value_or(fallback ? 1 : 0) != 0
                                        : fallback;
            };
            world_.dimension.min_y = integer("min_y", world_.dimension.min_y);
            world_.dimension.height = integer("height", world_.dimension.height);
            world_.dimension.has_skylight = flag("has_skylight", world_.dimension.has_skylight);
            world_.dimension.has_ceiling = flag("has_ceiling", world_.dimension.has_ceiling);
            if (const auto* value = tag.find("ambient_light"); value != nullptr) {
                world_.dimension.ambient_light =
                    static_cast<float>(value->as_double().value_or(0.0));
            }
        }
    }
}

bool Session::handle_world(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    if (packet.id == cb.login) {
        entity_id_ = in.read_i32();
        player_.hardcore = in.read_bool();
        const int32_t worlds = in.read_varint();
        for (int32_t i = 0; i < worlds; ++i) in.read_string();
        in.read_varint();   // max players, which the client has no use for
        in.read_varint();   // view distance
        in.read_varint();   // simulation distance
        in.read_bool();     // reduced debug info
        in.read_bool();     // respawn screen
        in.read_bool();     // limited crafting
        read_spawn_info(in);
        in.read_bool();     // enforces secure chat
        status(std::format("entered the world, entity {}, in {} from y {} for {}", entity_id_,
                           world_.dimension.name, world_.dimension.min_y, world_.dimension.height));
        return true;
    }
    if (packet.id == cb.respawn) {
        // The same spawn info as login, then a byte saying what of the player
        // to keep across it. A new dimension has its own height and its own
        // entities, so the old ones are dropped here and the client drops
        // its world on the event.
        read_spawn_info(in);
        // 0x01 keeps the attributes, 0x02 the entity data.
        const uint8_t kept = in.read_u8();
        entities_.entities.clear();
        entities_.effects.clear();
        if ((kept & 0x01) == 0) {
            entities_.attributes.clear();
        } else {
            std::erase_if(entities_.attributes,
                          [this](const auto& e) { return e.first != entity_id_; });
        }
        status(std::format("now in {}, from y {} for {}", world_.dimension.name,
                           world_.dimension.min_y, world_.dimension.height));
        if (events_.world.on_respawn) events_.world.on_respawn(world_.dimension);
        return true;
    }
    if (packet.id == cb.level_chunk_with_light) {
        if (!options_.chunk_dump.empty()) {
            std::ofstream out(options_.chunk_dump, std::ios::binary);
            out.write(reinterpret_cast<const char*>(in.data().data()),
                      static_cast<std::streamsize>(in.data().size()));
            options_.chunk_dump.clear();
            status("first chunk packet written out raw");
        }
        auto chunk = world::read_chunk(in, world_.dimension.min_y);
        // The packet should end exactly here. Anything left over means the
        // light format moved and we are reading the next chunk wrong.
        if (in.remaining() != 0 && !world_.light_format_warned) {
            world_.light_format_warned = true;
            status(std::format("{} bytes left after a chunk: the light format has changed",
                               in.remaining()));
        }
        if (events_.world.on_chunk) events_.world.on_chunk(chunk);
        return true;
    }
    if (packet.id == cb.chunk_batch_finished) {
        in.read_varint();
        ByteBuffer out;
        out.write_f32(kChunksPerTick);
        send(ids_->play_sb.chunk_batch_received, out);
        return true;
    }
    if (packet.id == cb.set_chunk_cache_center || packet.id == cb.set_chunk_cache_radius) {
        if (packet.id == cb.set_chunk_cache_center) {
            world_.area_x = in.read_varint();
            world_.area_z = in.read_varint();
        } else {
            world_.area_radius = in.read_varint();
        }
        // Until the server names a radius, it is the one this client asked
        // for, which is what the server will have used.
        const int32_t radius =
            world_.area_radius >= 0 ? world_.area_radius : options_.view_distance;
        if (events_.world.on_chunk_area) {
            events_.world.on_chunk_area(world_.area_x, world_.area_z, radius);
        }
        return true;
    }
    if (packet.id == cb.forget_level_chunk) {
        // One long, z in the high half and x in the low, which on the wire
        // reads as two ints with z first.
        const int32_t chunk_z = in.read_i32();
        const int32_t chunk_x = in.read_i32();
        if (events_.world.on_chunk_forget) events_.world.on_chunk_forget(chunk_x, chunk_z);
        return true;
    }
    if (packet.id == cb.light_update) {
        const int32_t chunk_x = in.read_varint();
        const int32_t chunk_z = in.read_varint();
        if (events_.world.on_light_update) events_.world.on_light_update(chunk_x, chunk_z, in);
        return true;
    }
    if (packet.id == cb.block_update) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        unpack_position(in.read_i64(), x, y, z);
        const int32_t state = in.read_varint();
        if (events_.world.on_block_change) events_.world.on_block_change(x, y, z, state);
        return true;
    }
    if (packet.id == cb.section_blocks_update) {
        // One long names the section, then every change in it is a varlong:
        // the state, then four bits each of x, z and y inside the section.
        const int64_t section = in.read_i64();
        const int32_t base_x = static_cast<int32_t>(section >> 42) * 16;
        const int32_t base_z = static_cast<int32_t>((section << 22) >> 42) * 16;
        const int32_t base_y = static_cast<int32_t>((section << 44) >> 44) * 16;
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int64_t entry = in.read_varlong();
            const int32_t state = static_cast<int32_t>(entry >> 12);
            if (!events_.world.on_block_change) continue;
            events_.world.on_block_change(base_x + static_cast<int32_t>((entry >> 8) & 0xF),
                                    base_y + static_cast<int32_t>(entry & 0xF),
                                    base_z + static_cast<int32_t>((entry >> 4) & 0xF), state);
        }
        return true;
    }
    if (packet.id == cb.game_event) {
        const auto event = in.read_u8();
        const float value = in.read_f32();
        // Three is the one that says the game mode changed, and the value is
        // the mode itself.
        if (event == 3) player_.vitals.game_mode = static_cast<int32_t>(value);
        return true;
    }
    return false;
}

} // namespace mcpp
