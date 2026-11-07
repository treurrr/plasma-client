#include "mcpp/session.hpp"

#include "mcpp/proto/components.hpp"
#include "mcpp/world/chunk.hpp"

#include <algorithm>
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

// The `ParticleOptions` behind a type's id. A type the game gives no codec of
// its own writes nothing, so most of them land in the default case.
void read_particle_options(ByteBuffer& in, LevelParticles& particles, int32_t protocol) {
    const std::string_view type = particles.type;
    if (type == "minecraft:block" || type == "minecraft:block_marker" ||
        type == "minecraft:falling_dust" || type == "minecraft:dust_pillar" ||
        type == "minecraft:block_crumble") {
        // `BlockParticleOption`: the block state's id.
        particles.block_state = in.read_varint();
    } else if (type == "minecraft:item") {
        // `ItemParticleOption`: a whole stack, of which we keep the item.
        const ItemStack stack = proto::read_item_stack(in, protocol);
        particles.item = stack.count > 0 ? stack.id : -1;
    } else if (type == "minecraft:dust") {
        // `DustParticleOptions`: the colour and how big the mote is.
        particles.colour = static_cast<uint32_t>(in.read_i32());
        particles.scale = in.read_f32();
    } else if (type == "minecraft:dust_color_transition") {
        // `DustColorTransitionOptions`: the colour it starts and ends on.
        particles.colour = static_cast<uint32_t>(in.read_i32());
        particles.to_colour = static_cast<uint32_t>(in.read_i32());
        particles.scale = in.read_f32();
    } else if (type == "minecraft:entity_effect" || type == "minecraft:tinted_leaves" ||
               type == "minecraft:flash") {
        // `ColorParticleOption`: one colour, alpha and all.
        particles.colour = static_cast<uint32_t>(in.read_i32());
    } else if (type == "minecraft:effect" || type == "minecraft:instant_effect") {
        // `SpellParticleOption`: a colour and how hard it is thrown.
        particles.colour = static_cast<uint32_t>(in.read_i32());
        particles.power = in.read_f32();
    } else if (type == "minecraft:dragon_breath" || type == "minecraft:sculk_charge") {
        // `PowerParticleOption`, `SculkChargeParticleOptions`: one float.
        particles.power = in.read_f32();
    } else if (type == "minecraft:shriek") {
        // `ShriekParticleOption`: how many ticks before it shows.
        in.read_varint();
    } else if (type == "minecraft:trail") {
        // `TrailParticleOption`: where it flies to, its colour and for how long.
        in.read_f64();
        in.read_f64();
        in.read_f64();
        particles.colour = static_cast<uint32_t>(in.read_i32());
        in.read_varint();
    } else if (type == "minecraft:vibration") {
        // `VibrationParticleOption`: a `PositionSource`, which is a block
        // position or an entity and an eye offset, and when it arrives.
        if (in.read_varint() == 0) {
            in.read_i64();
        } else {
            in.read_varint();
            in.read_f32();
        }
        in.read_varint();
    }
}

} // namespace

// What login and respawn both carry: which dimension, its type, the game mode
// and a few world facts. The type is an index into the dimension_type registry
// sent in configuration, and that entry is where the world's height lives.
void Session::read_spawn_info(ByteBuffer& in) {
    const int32_t type = in.read_varint();
    world_.dimension.name = in.read_string();
    world_.dimension.hashed_seed = in.read_i64();
    player_.vitals.game_mode = static_cast<int8_t>(in.read_u8());
    in.read_u8();    // previous game mode
    in.read_bool();  // debug world
    world_.dimension.flat_world = in.read_bool();
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
            const auto text = [&](std::string_view key, std::string fallback) {
                const auto* value = tag.find(key);
                const std::string* found = value != nullptr ? value->as_string() : nullptr;
                return found != nullptr ? *found : fallback;
            };
            auto& dimension = world_.dimension;
            dimension.min_y = integer("min_y", -64);
            dimension.height = integer("height", 384);
            dimension.logical_height = integer("logical_height", dimension.height);
            dimension.has_skylight = flag("has_skylight", true);
            dimension.has_ceiling = flag("has_ceiling", false);
            dimension.has_fixed_time = flag("has_fixed_time", false);
            dimension.ambient_light = 0.0f;
            if (const auto* value = tag.find("ambient_light"); value != nullptr) {
                dimension.ambient_light = static_cast<float>(value->as_double().value_or(0.0));
            }
            dimension.coordinate_scale = 1.0;
            if (const auto* value = tag.find("coordinate_scale"); value != nullptr) {
                dimension.coordinate_scale = value->as_double().value_or(1.0);
            }
            dimension.skybox = text("skybox", "overworld");
            dimension.cardinal_light = text("cardinal_light", "default");
            dimension.infiniburn = text("infiniburn", "");
            dimension.monster_spawn_block_light_limit =
                integer("monster_spawn_block_light_limit", 0);
        }
    }

    // The registries are all in by the time the player enters a world; the
    // environment is read from them once and pointed at each new dimension.
    if (!world_.environment_loaded) {
        world_.environment.load(data_.registries, data_.tags);
        world_.environment_loaded = true;
        read_biome_extras();
    }
    const bool has_weather = world_.dimension.has_skylight && !world_.dimension.has_ceiling &&
                             world_.dimension.name != "minecraft:the_end";
    if (!world_.environment.select(world_.dimension.type, has_weather)) {
        status(std::format("no environment for dimension type {}", world_.dimension.type));
    }
}

// What session.cpp's biome reader leaves out: the fixed dry foliage colour.
void Session::read_biome_extras() {
    const auto* entries = data_.registries.find("minecraft:worldgen/biome");
    if (entries == nullptr) return;
    world::BiomeTable table;
    for (size_t i = 0; i < world_.biomes.size(); ++i) {
        world::Biome biome = *world_.biomes.at(i);
        if (i < entries->size() && (*entries)[i].data) {
            if (const auto* rain = (*entries)[i].data->find("has_precipitation")) {
                biome.has_precipitation = rain->as_int().value_or(1) != 0;
            }
            if (const auto* effects = (*entries)[i].data->find("effects")) {
                if (const auto* value = effects->find("dry_foliage_color")) {
                    if (const auto rgb = world::parse_colour(*value, false)) {
                        biome.fixed_dry_foliage = true;
                        biome.dry_foliage = *rgb & 0xFFFFFFu;
                    }
                }
            }
        }
        table.add(std::move(biome));
    }
    world_.biomes = std::move(table);
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
        entities_.equipment.clear();
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
        if (events_.world.on_light_update) {
            events_.world.on_light_update(chunk_x, chunk_z, in);
        } else {
            in.read_bytes(in.remaining());
        }
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
        // 1 and 2 start and stop the rain; 7 and 8 are how far along rain and
        // thunder are, sent every tick while either changes. 3 is a new game
        // mode, the value being the mode itself. The rest are for other areas.
        switch (event) {
        case 1:
            world_.weather.raining = true;
            world_.weather.rain_level = 0.0f;
            break;
        case 2:
            world_.weather.raining = false;
            world_.weather.rain_level = 1.0f;
            break;
        case 3: player_.vitals.game_mode = static_cast<int32_t>(value); break;
        case 7: world_.weather.rain_level = std::clamp(value, 0.0f, 1.0f); break;
        case 8: world_.weather.thunder_level = std::clamp(value, 0.0f, 1.0f); break;
        default: break;
        }
        if (events_.world.on_game_event) events_.world.on_game_event(event, value);
        return true;
    }
    if (packet.id == cb.level_particles) {
        // `ClientboundLevelParticlesPacket`: the cloud, then the type's own
        // `ParticleOptions` behind its registry id.
        LevelParticles particles;
        particles.override_limiter = in.read_bool();
        particles.always_show = in.read_bool();
        particles.x = in.read_f64();
        particles.y = in.read_f64();
        particles.z = in.read_f64();
        particles.x_dist = in.read_f32();
        particles.y_dist = in.read_f32();
        particles.z_dist = in.read_f32();
        particles.max_speed = in.read_f32();
        particles.count = in.read_i32();
        const int32_t type = in.read_varint();
        if (const auto* entry = data_.registries.at("minecraft:particle_type",
                                                    static_cast<size_t>(std::max(0, type)))) {
            particles.type = entry->name;
        }
        read_particle_options(in, particles, options_.protocol);
        if (events_.world.on_particles) events_.world.on_particles(particles);
        return true;
    }
    if (packet.id == cb.set_time) {
        world_.time.game_time = in.read_i64();
        world_.time.day_time = in.read_i64();
        world_.time.ticking = in.read_bool();
        ++world_.time.updates;
        return true;
    }
    if (packet.id == cb.chunks_biomes) {
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int32_t chunk_z = in.read_i32();
            const int32_t chunk_x = in.read_i32();
            const int32_t length = in.read_varint();
            if (length < 0) fail("negative biome data length");
            ByteBuffer data(in.read_bytes(static_cast<size_t>(length)));
            std::vector<world::PalettedContainer> sections;
            while (data.remaining() > 0) {
                sections.push_back(
                    world::PalettedContainer::read(data, world::kBiomesPerSection, 3));
            }
            if (events_.world.on_chunk_biomes) {
                events_.world.on_chunk_biomes(chunk_x, chunk_z, sections);
            }
        }
        return true;
    }
    if (packet.id == cb.block_entity_data) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        unpack_position(in.read_i64(), x, y, z);
        const int32_t type = in.read_varint();
        const nbt::Value data = nbt::read_network_tag(in);
        if (events_.world.on_block_entity) events_.world.on_block_entity(x, y, z, type, data);
        return true;
    }
    if (packet.id == cb.block_event) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        unpack_position(in.read_i64(), x, y, z);
        const uint8_t action = in.read_u8();
        const uint8_t parameter = in.read_u8();
        in.read_varint();   // the block the server thinks is there
        if (events_.world.on_block_event) {
            events_.world.on_block_event(x, y, z, action, parameter);
        }
        return true;
    }
    if (packet.id == cb.initialize_border || packet.id == cb.set_border_center ||
        packet.id == cb.set_border_lerp_size || packet.id == cb.set_border_size ||
        packet.id == cb.set_border_warning_delay || packet.id == cb.set_border_warning_distance) {
        auto& border = world_.border;
        if (packet.id == cb.initialize_border) {
            border.centre_x = in.read_f64();
            border.centre_z = in.read_f64();
            border.size = in.read_f64();
            border.target = in.read_f64();
            border.lerp_ms = in.read_varlong();
            border.absolute_max = in.read_varint();
            border.warning_blocks = in.read_varint();
            border.warning_seconds = in.read_varint();
        } else if (packet.id == cb.set_border_center) {
            border.centre_x = in.read_f64();
            border.centre_z = in.read_f64();
        } else if (packet.id == cb.set_border_lerp_size) {
            border.size = in.read_f64();
            border.target = in.read_f64();
            border.lerp_ms = in.read_varlong();
        } else if (packet.id == cb.set_border_size) {
            border.size = in.read_f64();
            border.target = border.size;
            border.lerp_ms = 0;
        } else if (packet.id == cb.set_border_warning_delay) {
            border.warning_seconds = in.read_varint();
        } else {
            border.warning_blocks = in.read_varint();
        }
        if (events_.world.on_border) events_.world.on_border(border);
        return true;
    }
    return false;
}

} // namespace mcpp
