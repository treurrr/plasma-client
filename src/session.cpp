#include "mcpp/session.hpp"
#include "session_detail.hpp"

#include "mcpp/auth.hpp"
#include "mcpp/crypto.hpp"

#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"

#include <charconv>
#include <fstream>
#include <format>

namespace mcpp {
namespace detail {

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

} // namespace detail

namespace {

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
constexpr int kIdleTimeoutMs = 40000;      // keep-alives arrive every 15s

void write_nil_uuid(ByteBuffer& out) {
    for (size_t i = 0; i < detail::kUuidBytes; ++i) out.write_u8(0);
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
    data_.registries.load_builtin(Registries::builtin_path(options_.protocol));
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
    if (playback_.is_open()) return;   // a recording has nobody to answer
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

        if (packet.id == cb.login_compression) {
            const int32_t threshold = packet.data.read_varint();
            connection_.set_compression_threshold(threshold);
            status(std::format("compression from {} bytes", threshold));
            continue;
        }
        if (packet.id == cb.login_finished) {
            profile_uuid_ = detail::read_uuid(packet.data);
            profile_name_ = packet.data.read_string(16);
            send(ids_->login_sb.login_acknowledged, ByteBuffer{});
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
        if (packet.id == cb.login_disconnect) {
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
    if (packet.id == cb.update_tags) {
        read_tags(in);
        return true;
    }
    if (packet.id == cb.registry_data) {
        // Read every registry whole, whatever it is, and keep it: the order of
        // its entries is what ids elsewhere index into.
        std::string registry = in.read_string();
        const int32_t count = in.read_varint();
        if (count < 0) fail("negative registry entry count");
        std::vector<RegistryEntry> entries;
        entries.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            RegistryEntry entry;
            entry.name = in.read_string();
            if (in.read_bool()) entry.data = nbt::read_network_tag(in);
            entries.push_back(std::move(entry));
        }
        if (registry == "minecraft:worldgen/biome") {
            world_.biomes.clear();
            for (const auto& entry : entries) {
                world::Biome biome;
                biome.name = entry.name;
                if (entry.data.has_value()) {
                    const auto& tag = *entry.data;
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
                world_.biomes.add(std::move(biome));
            }
            status(std::format("{} biomes from the server", world_.biomes.size()));
        }
        data_.registries.set(std::move(registry), std::move(entries));
        return true;
    }
    if (packet.id == cb.resource_pack_push) {
        ByteBuffer out;
        out.write_bytes(in.read_bytes(detail::kUuidBytes));
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
    if (packet.id == cb.finish_configuration) {
        send(ids_->config_sb.finish_configuration, ByteBuffer{});
        state_ = SessionState::Play;
        status("in the play state");
        return true;
    }
    if (packet.id == cb.disconnect) {
        fail(std::format("server closed the connection during configuration: {}",
                         detail::read_component(in, lookup_)));
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
    if (packet.id == cb.start_configuration) {
        send(ids_->play_sb.configuration_acknowledged, ByteBuffer{});
        begin_configuration();
        status("server sent us back to configuration");
        return true;
    }
    if (packet.id == cb.disconnect) {
        fail(std::format("server closed the connection: {}", detail::read_component(in, lookup_)));
    }

    // Every other packet belongs to one area of the game, and each area is
    // asked in turn. One that none of them knows is ignored, which is fine;
    // pump says so only when one was read half way.
    if (handle_world(packet) || handle_entities(packet) || handle_player(packet) ||
        handle_ui(packet) || handle_sound(packet) || handle_data(packet)) {
        return true;
    }
    return true;
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

    if (!options_.replay.empty()) {
        playback_.open(options_.replay, std::ios::binary);
        char magic[4]{};
        playback_.read(magic, 4);
        if (!playback_ || std::string_view(magic, 4) != "PLRC") {
            fail(std::format("{} is not a recording", options_.replay.string()));
        }
        profile_name_ = options_.username;
        state_ = SessionState::Configuration;
        status(std::format("playing back {}", options_.replay.string()));
        return;
    }

    handshake();
    run_login();
    begin_configuration();
    if (!options_.record.empty()) {
        recording_.open(options_.record, std::ios::binary | std::ios::trunc);
        recording_.write("PLRC", 4);
    }
}

bool Session::has_data() const {
    if (playback_.is_open()) return playback_.good() && playback_.rdbuf()->in_avail() != 0;
    return connection_.socket().readable();
}

// One record: the state it arrived in, the id, the length and the bytes.
bool Session::next_from_playback(PacketConnection::Packet& packet) {
    uint8_t state = 0;
    uint32_t length = 0;
    playback_.read(reinterpret_cast<char*>(&state), 1);
    playback_.read(reinterpret_cast<char*>(&packet.id), sizeof(packet.id));
    playback_.read(reinterpret_cast<char*>(&length), sizeof(length));
    if (!playback_) return false;
    std::vector<uint8_t> bytes(length);
    playback_.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
    if (!playback_) return false;
    packet.data = ByteBuffer(std::move(bytes));
    return true;
}

bool Session::pump() {
    if (state_ == SessionState::Closed) return false;
    PacketConnection::Packet packet;
    if (playback_.is_open()) {
        if (!next_from_playback(packet)) {
            state_ = SessionState::Closed;
            return false;
        }
    } else {
        packet = connection_.receive();
        if (recording_.is_open() && (state_ == SessionState::Configuration ||
                                     state_ == SessionState::Play)) {
            const auto state = static_cast<uint8_t>(state_ == SessionState::Play ? 2 : 1);
            const auto& bytes = packet.data.data();
            const auto length = static_cast<uint32_t>(bytes.size());
            recording_.write(reinterpret_cast<const char*>(&state), 1);
            recording_.write(reinterpret_cast<const char*>(&packet.id), sizeof(packet.id));
            recording_.write(reinterpret_cast<const char*>(&length), sizeof(length));
            recording_.write(reinterpret_cast<const char*>(bytes.data()), length);
        }
    }
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

} // namespace mcpp
