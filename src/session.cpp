#include "mcpp/session.hpp"

#include "mcpp/auth.hpp"
#include "mcpp/crypto.hpp"

#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/world/chunk.hpp"

#include <charconv>
#include <chrono>
#include <fstream>
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

int64_t pack_position(int32_t x, int32_t y, int32_t z) {
    return ((static_cast<int64_t>(x) & 0x3FFFFFF) << 38) |
           ((static_cast<int64_t>(z) & 0x3FFFFFF) << 12) |
           (static_cast<int64_t>(y) & 0xFFF);
}

// A colour in the biome registry arrives either as a packed int or as the text
// "#rrggbb", depending on how the server's datapack was written.
std::optional<uint32_t> read_colour(const nbt::Value& value) {
    if (const auto number = value.as_int()) return static_cast<uint32_t>(*number) & 0xFFFFFFu;
    const std::string* text = value.as_string();
    if (text == nullptr) return std::nullopt;
    std::string_view digits = *text;
    if (!digits.empty() && digits.front() == '#') digits.remove_prefix(1);
    uint32_t rgb = 0;
    const char* end = digits.data() + digits.size();
    const auto parsed = std::from_chars(digits.data(), end, rgb, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return rgb & 0xFFFFFFu;
}

constexpr int32_t kNextStateLogin = 2;
constexpr size_t kUuidBytes = 16;
constexpr size_t kSignatureBytes = 256;
constexpr size_t kAcknowledgedBytes = 3;   // Fixed BitSet(20)
constexpr int kIdleTimeoutMs = 40000;      // keep-alives arrive every 15s
constexpr float kChunksPerTick = 32.0f;    // the server throttles to what we claim

void write_nil_uuid(ByteBuffer& out) {
    for (size_t i = 0; i < kUuidBytes; ++i) out.write_u8(0);
}

std::string read_uuid(ByteBuffer& in) {
    auto bytes = in.read_bytes(kUuidBytes);
    std::string out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += std::format("{:02x}", bytes[i]);
    }
    return out;
}

std::string read_component(ByteBuffer& in, const nbt::TranslateLookup& lookup) {
    return json::strip_formatting(nbt::component_to_text(nbt::read_network_tag(in), lookup));
}

// Layout read off the wire for 774: a signed message carries the plain text and,
// when the server decorated it, an NBT component that supersedes it.
std::string read_player_chat(ByteBuffer& in, const nbt::TranslateLookup& lookup) {
    in.read_varint();
    in.read_bytes(kUuidBytes);
    in.read_varint();
    if (in.read_bool()) in.read_bytes(kSignatureBytes);

    std::string body = in.read_string(256);
    in.read_i64();
    in.read_i64();

    const int32_t previous = in.read_varint();
    for (int32_t i = 0; i < previous; ++i) {
        if (in.read_varint() == 0) in.read_bytes(kSignatureBytes);
    }

    if (in.read_bool()) body = nbt::component_to_text(nbt::read_network_tag(in), lookup);

    if (in.read_varint() == 2) {
        const int32_t longs = in.read_varint();
        for (int32_t i = 0; i < longs; ++i) in.read_i64();
    }

    if (in.read_varint() == 0) return body;   // inline chat type: no name to read

    const auto sender = nbt::component_to_text(nbt::read_network_tag(in), lookup);
    return sender.empty() ? body : std::format("<{}> {}", sender, body);
}

int64_t now_millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

std::string_view state_name(SessionState state) {
    switch (state) {
    case SessionState::Handshaking: return "handshake";
    case SessionState::Login:       return "login";
    case SessionState::Configuration: return "configuration";
    case SessionState::Play:        return "play";
    case SessionState::Closed:      return "closed";
    }
    return "?";
}

Session::Session(SessionOptions options, SessionEvents events)
    : options_(std::move(options)), events_(std::move(events)) {
    if (translations_.load(options_.lang_file)) {
        lookup_ = [this](std::string_view key) { return translations_.find(key); };
    }

    ids_ = proto::ids_for(options_.protocol);
    if (!ids_) {
        throw ProtocolError(std::format("no packet table for protocol {}; generate one with "
                                        "tools/gen_packet_ids.py", options_.protocol));
    }
}

void Session::status(std::string_view line) const {
    if (events_.on_status) events_.on_status(line);
}

void Session::fail(std::string reason) {
    state_ = SessionState::Closed;
    if (events_.on_disconnect) events_.on_disconnect(reason);
    throw ProtocolError(std::move(reason));
}

void Session::send(int32_t id, const ByteBuffer& payload) {
    std::lock_guard guard(send_mutex_);
    connection_.send(id, payload);
}

void Session::handshake() {
    TcpSocket socket;
    socket.connect(options_.host, options_.port);
    connection_ = PacketConnection(std::move(socket));

    ByteBuffer out;
    out.write_varint(options_.protocol);
    out.write_string(options_.host);
    out.write_u16(options_.port);
    out.write_varint(kNextStateLogin);
    send(0x00, out);

    state_ = SessionState::Login;
    status(std::format("connected to {}:{} as protocol {}", options_.host, options_.port,
                       options_.protocol));
    if (translations_.empty()) {
        status(std::format("no language file at {}; server messages stay raw keys "
                           "(tools/fetch_lang.sh)", options_.lang_file.string()));
    }
}

void Session::run_login() {
    ByteBuffer hello;
    hello.write_string(options_.profile.valid() ? options_.profile.name : options_.username);
    write_nil_uuid(hello);
    send(ids_->login_sb.hello, hello);

    for (;;) {
        auto packet = connection_.receive();
        const auto& cb = ids_->login_cb;

        if (packet.id == cb.compression) {
            const int32_t threshold = packet.data.read_varint();
            connection_.set_compression_threshold(threshold);
            status(std::format("compression from {} bytes", threshold));
            continue;
        }
        if (packet.id == cb.finished) {
            profile_uuid_ = read_uuid(packet.data);
            profile_name_ = packet.data.read_string(16);
            send(ids_->login_sb.acknowledged, ByteBuffer{});
            status(std::format("logged in as {} ({})", profile_name_, profile_uuid_));
            return;
        }
        if (packet.id == cb.hello) {
            const std::string server_id = packet.data.read_string(20);
            const auto key = packet.data.read_bytes(
                static_cast<size_t>(packet.data.read_varint()));
            const auto token = packet.data.read_bytes(
                static_cast<size_t>(packet.data.read_varint()));
            const bool authenticate = packet.data.read_bool();

            const auto secret = crypto::random_secret();

            if (authenticate) {
                if (!options_.profile.valid()) {
                    fail("this server is in online mode and needs a signed-in account");
                }
                const auto hash = crypto::server_hash(server_id, secret, key);
                if (const auto problem = auth::join_server(options_.profile, hash);
                    !problem.empty()) {
                    fail(std::format("could not join as {}: {}", options_.profile.name, problem));
                }
                status(std::format("session accepted for {}", options_.profile.name));
            }

            ByteBuffer out;
            const auto sealed_secret = crypto::rsa_encrypt(key, secret);
            const auto sealed_token = crypto::rsa_encrypt(key, token);
            out.write_varint(static_cast<int32_t>(sealed_secret.size()));
            out.write_bytes(sealed_secret);
            out.write_varint(static_cast<int32_t>(sealed_token.size()));
            out.write_bytes(sealed_token);
            send(ids_->login_sb.key, out);

            // Everything after this packet is encrypted, including the reply.
            connection_.start_encryption(secret);
            status("encryption on");
            continue;
        }
        if (packet.id == cb.disconnect) {
            fail(std::format("server refused the login: {}",
                             json::strip_formatting(
                                 json::flatten_component(json::parse(packet.data.read_string())))));
        }
        if (packet.id == cb.custom_query) {
            ByteBuffer answer;
            answer.write_varint(packet.data.read_varint());
            answer.write_bool(false);
            send(ids_->login_sb.custom_query_answer, answer);
            continue;
        }
        if (packet.id == cb.cookie_request) {
            ByteBuffer answer;
            answer.write_string(packet.data.read_string());
            answer.write_bool(false);
            send(ids_->login_sb.cookie_response, answer);
            continue;
        }
        fail(std::format("unexpected login packet 0x{:02X}", packet.id));
    }
}

void Session::send_client_information() {
    ByteBuffer out;
    out.write_string(options_.locale);
    out.write_u8(options_.view_distance);
    out.write_varint(0);      // chat mode: enabled
    out.write_bool(true);     // chat colors
    out.write_u8(0x7F);       // all skin parts
    out.write_varint(1);      // main hand: right
    out.write_bool(false);    // text filtering
    out.write_bool(true);     // server listings
    out.write_varint(0);      // particles: all
    send(ids_->config_sb.client_information, out);
}

void Session::begin_configuration() {
    state_ = SessionState::Configuration;
    connection_.socket().set_receive_timeout(kIdleTimeoutMs);
    send_client_information();
}

bool Session::handle_configuration(PacketConnection::Packet& packet) {
    const auto& cb = ids_->config_cb;
    auto& in = packet.data;

    if (packet.id == cb.keep_alive) {
        ByteBuffer out;
        out.write_i64(in.read_i64());
        send(ids_->config_sb.keep_alive, out);
        return true;
    }
    if (packet.id == cb.ping) {
        ByteBuffer out;
        out.write_i32(in.read_i32());
        send(ids_->config_sb.pong, out);
        return true;
    }
    if (packet.id == cb.select_known_packs) {
        ByteBuffer out;
        out.write_varint(0);
        send(ids_->config_sb.select_known_packs, out);
        status("no known packs reported; server sends the full registries");
        return true;
    }
    if (packet.id == cb.registry_data) {
        const std::string registry = in.read_string();
        const int32_t count = in.read_varint();
        if (count < 0) fail("negative registry entry count");
        if (registry != "minecraft:worldgen/biome") return true;

        biomes_.clear();
        for (int32_t i = 0; i < count; ++i) {
            world::Biome biome;
            biome.name = in.read_string();
            if (in.read_bool()) {
                const auto tag = nbt::read_network_tag(in);
                if (const auto* value = tag.find("temperature"); value != nullptr) {
                    biome.temperature = static_cast<float>(value->as_double().value_or(0.5));
                }
                if (const auto* value = tag.find("downfall"); value != nullptr) {
                    biome.downfall = static_cast<float>(value->as_double().value_or(0.5));
                }
                if (const auto* effects = tag.find("effects"); effects != nullptr) {
                    if (const auto* value = effects->find("grass_color"); value != nullptr) {
                        if (const auto rgb = read_colour(*value)) {
                            biome.fixed_grass = true;
                            biome.grass = *rgb;
                        }
                    }
                    if (const auto* value = effects->find("foliage_color"); value != nullptr) {
                        if (const auto rgb = read_colour(*value)) {
                            biome.fixed_foliage = true;
                            biome.foliage = *rgb;
                        }
                    }
                    if (const auto* value = effects->find("grass_color_modifier");
                        value != nullptr) {
                        const std::string* kind = value->as_string();
                        if (kind != nullptr && *kind == "swamp") {
                            biome.grass_modifier = world::Biome::GrassModifier::Swamp;
                        } else if (kind != nullptr && *kind == "dark_forest") {
                            biome.grass_modifier = world::Biome::GrassModifier::DarkForest;
                        }
                    }
                    if (const auto* value = effects->find("water_color"); value != nullptr) {
                        biome.water = read_colour(*value).value_or(0x3F76E4u);
                    }
                }
            }
            biomes_.add(std::move(biome));
        }
        status(std::format("{} biomes from the server", biomes_.size()));
        return true;
    }
    if (packet.id == cb.resource_pack_push) {
        ByteBuffer out;
        out.write_bytes(in.read_bytes(kUuidBytes));
        out.write_varint(1);   // declined
        send(ids_->config_sb.resource_pack, out);
        return true;
    }
    if (packet.id == cb.code_of_conduct) {
        const auto text = in.read_string();
        status(std::format("server code of conduct:\n{}", text));
        if (!options_.accept_code_of_conduct) {
            fail("code of conduct not accepted");
        }
        send(ids_->config_sb.accept_code_of_conduct, ByteBuffer{});
        return true;
    }
    if (packet.id == cb.finish) {
        send(ids_->config_sb.finish, ByteBuffer{});
        state_ = SessionState::Play;
        status("in the play state");
        return true;
    }
    if (packet.id == cb.disconnect) {
        fail(std::format("server closed the connection during configuration: {}",
                         read_component(in, lookup_)));
    }
    return true;
}

bool Session::handle_play(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    if (options_.debug_packets) {
        std::string hex;
        const auto& bytes = in.data();
        for (size_t i = 0; i < bytes.size() && i < 160; ++i) hex += std::format("{:02x}", bytes[i]);
        status(std::format("play 0x{:02X} {} bytes: {}", packet.id, bytes.size(), hex));
    }

    if (packet.id == cb.keep_alive) {
        ByteBuffer out;
        out.write_i64(in.read_i64());
        send(ids_->play_sb.keep_alive, out);
        return true;
    }
    if (packet.id == cb.ping) {
        ByteBuffer out;
        out.write_i32(in.read_i32());
        send(ids_->play_sb.pong, out);
        return true;
    }
    if (packet.id == cb.login) {
        entity_id_ = in.read_i32();
        status(std::format("entered the world, entity {}", entity_id_));
        return true;
    }
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
        last_x_ = pick(0x01, x, last_x_);
        last_y_ = pick(0x02, y, last_y_);
        last_z_ = pick(0x04, z, last_z_);
        last_yaw_ = static_cast<float>(pick(0x08, yaw, last_yaw_));
        last_pitch_ = static_cast<float>(pick(0x10, pitch, last_pitch_));
        if (events_.on_position) {
            events_.on_position(last_x_, last_y_, last_z_, last_yaw_, last_pitch_);
        }
        ByteBuffer out;
        out.write_varint(teleport);
        send(ids_->play_sb.accept_teleportation, out);
        // Since 1.21.2 the server keeps the player hidden from everyone else
        // until the client says it has finished loading. Without this you are
        // in the world, you can walk and chat, and nobody can see you.
        if (!announced_loaded_) {
            announced_loaded_ = true;
            ByteBuffer loaded;
            send(ids_->play_sb.player_loaded, loaded);
            status("told the server we are loaded");
        }
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
        auto chunk = world::read_chunk(in);
        // The packet should end exactly here. Anything left over means the
        // light format moved and we are reading the next chunk wrong.
        if (in.remaining() != 0 && !light_format_warned_) {
            light_format_warned_ = true;
            status(std::format("{} bytes left after a chunk: the light format has changed",
                               in.remaining()));
        }
        if (events_.on_chunk) events_.on_chunk(chunk);
        return true;
    }
    if (packet.id == cb.chunk_batch_finished) {
        in.read_varint();
        ByteBuffer out;
        out.write_f32(kChunksPerTick);
        send(ids_->play_sb.chunk_batch_received, out);
        return true;
    }
    if (packet.id == cb.player_chat) {
        const auto text = json::strip_formatting(read_player_chat(in, lookup_));
        if (events_.on_chat && !text.empty()) events_.on_chat(text);
        return true;
    }
    if (packet.id == cb.system_chat) {
        const auto text = read_component(in, lookup_);
        if (events_.on_chat && !text.empty()) events_.on_chat(text);
        return true;
    }
    if (packet.id == cb.start_configuration) {
        send(ids_->play_sb.configuration_acknowledged, ByteBuffer{});
        begin_configuration();
        status("server sent us back to configuration");
        return true;
    }
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
        const auto& stored = entities_[entity.id] = entity;
        if (events_.on_entity_add) events_.on_entity_add(stored);
        return true;
    }
    if (packet.id == cb.entity_position_sync) {
        const int32_t id = in.read_varint();
        Entity& entity = entities_[id];
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
        if (events_.on_entity_move) events_.on_entity_move(entity);
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
        const auto found = entities_.find(id);
        if (found != entities_.end()) {
            Entity& entity = found->second;
            entity.x += dx;
            entity.y += dy;
            entity.z += dz;
            if (turned) {
                entity.yaw = yaw;
                entity.pitch = pitch;
            }
            entity.on_ground = on_ground;
            if (events_.on_entity_move) events_.on_entity_move(entity);
        }
        return true;
    }
    if (packet.id == cb.remove_entities) {
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            const int32_t id = in.read_varint();
            entities_.erase(id);
            if (events_.on_entity_remove) events_.on_entity_remove(id);
        }
        return true;
    }
    if (packet.id == cb.set_chunk_cache_center || packet.id == cb.set_chunk_cache_radius) {
        if (packet.id == cb.set_chunk_cache_center) {
            area_x_ = in.read_varint();
            area_z_ = in.read_varint();
        } else {
            area_radius_ = in.read_varint();
        }
        // Until the server names a radius, it is the one this client asked
        // for, which is what the server will have used.
        const int32_t radius = area_radius_ >= 0 ? area_radius_ : options_.view_distance;
        if (events_.on_chunk_area) events_.on_chunk_area(area_x_, area_z_, radius);
        return true;
    }
    if (packet.id == cb.forget_level_chunk) {
        // One long, z in the high half and x in the low, which on the wire
        // reads as two ints with z first.
        const int32_t chunk_z = in.read_i32();
        const int32_t chunk_x = in.read_i32();
        if (events_.on_chunk_forget) events_.on_chunk_forget(chunk_x, chunk_z);
        return true;
    }
    if (packet.id == cb.light_update) {
        const int32_t chunk_x = in.read_varint();
        const int32_t chunk_z = in.read_varint();
        if (events_.on_light_update) events_.on_light_update(chunk_x, chunk_z, in);
        return true;
    }
    if (packet.id == cb.block_update) {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        unpack_position(in.read_i64(), x, y, z);
        const int32_t state = in.read_varint();
        if (events_.on_block_change) events_.on_block_change(x, y, z, state);
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
            if (!events_.on_block_change) continue;
            events_.on_block_change(base_x + static_cast<int32_t>((entry >> 8) & 0xF),
                                    base_y + static_cast<int32_t>(entry & 0xF),
                                    base_z + static_cast<int32_t>((entry >> 4) & 0xF), state);
        }
        return true;
    }
    if (packet.id == cb.block_changed_ack) {
        in.read_varint();   // the sequence we sent; nothing to undo yet
        return true;
    }
    if (packet.id == cb.game_event) {
        const auto event = in.read_u8();
        const float value = in.read_f32();
        // Three is the one that says the game mode changed, and the value is
        // the mode itself.
        if (event == 3) vitals_.game_mode = static_cast<int32_t>(value);
        return true;
    }
    if (packet.id == cb.set_health) {
        vitals_.health = in.read_f32();
        vitals_.food = in.read_varint();
        vitals_.saturation = in.read_f32();
        // The game shows a death screen and waits for the player to press
        // respawn. Until there is one, a dead player would stay dead forever:
        // invisible to everyone, selected by nothing, on zero hearts.
        if (vitals_.health <= 0.0f) {
            ByteBuffer out;
            out.write_varint(0);   // perform respawn
            send(ids_->play_sb.client_command, out);
            status("died; asked the server to respawn");
        }
        return true;
    }
    if (packet.id == cb.set_experience) {
        vitals_.experience = in.read_f32();
        vitals_.level = in.read_varint();
        return true;
    }
    if (packet.id == cb.set_held_slot) {
        const int32_t slot = in.read_varint();
        if (slot >= 0 && slot < static_cast<int32_t>(Inventory::kHotbarSlots)) {
            inventory_.held = slot;
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
            if (i < static_cast<int32_t>(Inventory::kSlots)) inventory_.slots[i] = stack;
        }
        if (readable) inventory_.carried = read_slot(in, readable);
        if (!readable && !slot_format_warned_) {
            slot_format_warned_ = true;
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
            inventory_.slots[slot] = stack;
        }
        return true;
    }
    if (packet.id == cb.player_info_update) {
        read_player_list(in);
        return true;
    }
    if (packet.id == cb.disconnect) {
        fail(std::format("server closed the connection: {}", read_component(in, lookup_)));
    }
    return true;
}

// The game measures latency itself and publishes it in the player list, so the
// number the hud shows is the server's own, not something we estimate.
void Session::read_player_list(ByteBuffer& in) {
    if (player_list_broken_) return;

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
            const auto uuid = read_uuid(in);
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
                        in.read_bytes(kUuidBytes);
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
            if (latency >= 0 && uuid == profile_uuid_) ping_ms_ = latency;
        }

        // The packet has to end here. Anything left means the action list grew
        // and everything after this point was read as the wrong field.
        if (in.remaining() != 0) throw ProtocolError("trailing bytes");
    } catch (const std::exception& error) {
        player_list_broken_ = true;
        ping_ms_ = -1;
        status(std::format("player list not understood ({}); no ping from the server",
                           error.what()));
    }
}

void Session::connect() {
    if (!options_.profile.valid()) {
#ifdef MCPP_DEV_OFFLINE
        status("development build: no ownership check, plays offline mode only");
#else
        fail("not logged in; this client checks ownership of the game before playing, "
             "including on offline-mode servers");
#endif
    }

    handshake();
    run_login();
    begin_configuration();
}

bool Session::has_data() const { return connection_.socket().readable(); }

bool Session::pump() {
    if (state_ == SessionState::Closed) return false;
    auto packet = connection_.receive();
    if (state_ == SessionState::Configuration) return handle_configuration(packet);
    if (state_ == SessionState::Play) {
        const size_t size = packet.data.remaining();
        const bool keep = handle_play(packet);
        // A handler that read some of a packet and then stopped has read the
        // layout wrong, or knows it reads only part of it. Either way it is
        // worth saying once: a packet left untouched is one we ignore, and
        // that is fine; one half read is how a wrong field order hides.
        const size_t left = packet.data.remaining();
        if (left != 0 && left != size && part_read_.insert(packet.id).second) {
            status(std::format("packet {} read {} of {} bytes", packet.id, size - left, size));
        }
        return keep;
    }
    return true;
}

// Closing the socket is what unblocks a reader thread sitting in recv.
void Session::close() {
    state_ = SessionState::Closed;
    connection_.socket().close();
}

void Session::send_position(double x, double y, double z, float yaw, float pitch,
                            bool on_ground) {
    if (state_ != SessionState::Play) return;
    last_x_ = x;
    last_y_ = y;
    last_z_ = z;
    last_yaw_ = yaw;
    last_pitch_ = pitch;

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
    if (slot == inventory_.held) return;
    inventory_.held = slot;
    ByteBuffer out;
    out.write_u16(static_cast<uint16_t>(slot));
    send(ids_->play_sb.set_carried_item, out);
}

void Session::send_dig(int32_t x, int32_t y, int32_t z, int32_t face, bool finished) {
    ByteBuffer out;
    out.write_varint(finished ? 2 : 0);   // finished digging, or started
    out.write_i64(pack_position(x, y, z));
    out.write_u8(static_cast<uint8_t>(face));
    out.write_varint(++sequence_);
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
    out.write_varint(++sequence_);
    send(ids_->play_sb.use_item_on, out);
}

void Session::send_swing() {
    ByteBuffer out;
    out.write_varint(0);   // the main hand
    send(ids_->play_sb.swing, out);
}

void Session::send_chat(std::string_view message) {
    if (state_ != SessionState::Play) throw ProtocolError("chat only works in the play state");

    ByteBuffer out;
    out.write_string(message);
    out.write_i64(now_millis());
    out.write_i64(0);          // salt
    out.write_bool(false);     // unsigned: needs enforce-secure-chat=false
    out.write_varint(0);       // message count
    for (size_t i = 0; i < kAcknowledgedBytes; ++i) out.write_u8(0);
    out.write_u8(0);           // checksum
    send(ids_->play_sb.chat, out);
}

void Session::send_command(std::string_view command) {
    if (state_ != SessionState::Play) throw ProtocolError("commands only work in the play state");

    ByteBuffer out;
    out.write_string(command);
    send(ids_->play_sb.chat_command, out);
}

} // namespace mcpp
