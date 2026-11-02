#pragma once
#include "mcpp/packet_io.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/world/chunk.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace mcpp {

enum class SessionState { Handshaking, Login, Configuration, Play, Closed };

std::string_view state_name(SessionState state);

struct SessionOptions {
    std::string host;
    uint16_t port = 25565;
    std::string username;
    int32_t protocol = protocol::kTargetProtocol;
    std::string locale = "en_us";
    uint8_t view_distance = 8;
    bool accept_code_of_conduct = true;
    bool debug_packets = false;
    std::filesystem::path lang_file = Translations::default_path();
    std::filesystem::path chunk_dump;
};

struct SessionEvents {
    std::function<void(std::string_view)> on_status;
    std::function<void(std::string_view)> on_chat;
    std::function<void(std::string_view)> on_disconnect;
    std::function<void(const world::Chunk&)> on_chunk;
};

// Offline mode only: a server in online-mode sends an encryption request, which
// needs a Microsoft session we do not have yet.
class Session {
public:
    Session(SessionOptions options, SessionEvents events);

    void connect();
    bool pump();
    void close();
    void send_chat(std::string_view message);
    void send_command(std::string_view command);

    SessionState state() const { return state_; }
    const std::string& profile_name() const { return profile_name_; }
    const std::string& profile_uuid() const { return profile_uuid_; }
    int32_t entity_id() const { return entity_id_; }

private:
    void handshake();
    void run_login();
    void begin_configuration();
    bool handle_configuration(PacketConnection::Packet& packet);
    bool handle_play(PacketConnection::Packet& packet);
    void send_client_information();
    void send(int32_t id, const ByteBuffer& payload);
    void fail(std::string reason);
    void status(std::string_view line) const;

    SessionOptions options_;
    SessionEvents events_;
    const proto::PacketIds* ids_ = nullptr;
    Translations translations_;
    nbt::TranslateLookup lookup_;
    PacketConnection connection_;
    std::mutex send_mutex_;
    SessionState state_ = SessionState::Handshaking;
    std::string profile_name_;
    std::string profile_uuid_;
    int32_t entity_id_ = -1;
};

} // namespace mcpp
