#include "mcpp/session.hpp"

#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/world/chunk.hpp"

#include <chrono>
#include <fstream>
#include <format>

namespace mcpp {
namespace {

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
            fail("server is in online mode and asks for encryption; Microsoft sessions come later");
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
        ByteBuffer out;
        out.write_varint(in.read_varint());
        send(ids_->play_sb.accept_teleportation, out);
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
    if (packet.id == cb.disconnect) {
        fail(std::format("server closed the connection: {}", read_component(in, lookup_)));
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

    handshake();
    run_login();
    begin_configuration();
}

bool Session::pump() {
    if (state_ == SessionState::Closed) return false;
    auto packet = connection_.receive();
    if (state_ == SessionState::Configuration) return handle_configuration(packet);
    if (state_ == SessionState::Play) return handle_play(packet);
    return true;
}

// Closing the socket is what unblocks a reader thread sitting in recv.
void Session::close() {
    state_ = SessionState::Closed;
    connection_.socket().close();
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
