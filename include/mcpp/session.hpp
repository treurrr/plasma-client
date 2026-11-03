#pragma once
#include "mcpp/auth.hpp"
#include "mcpp/packet_io.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/registries.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/world/biomes.hpp"
#include "mcpp/world/chunk.hpp"

#include <array>
#include <map>
#include <set>
#include <fstream>
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

    // Proof that this account owns the game, from Microsoft's own flow. A
    // release build refuses to connect without it, to any server, in any mode:
    // ownership is checked before play, not per server.
    auth::Profile profile;

    // Only used by a development build, where the gate is compiled out.
    std::string username;
    int32_t protocol = protocol::kTargetProtocol;
    std::string locale = "en_us";
    uint8_t view_distance = 8;
    bool accept_code_of_conduct = true;
    bool debug_packets = false;
    std::filesystem::path lang_file = Translations::default_path();
    std::filesystem::path chunk_dump;
    // A recording is every packet from configuration onwards, as it arrived.
    // Played back, the same handlers see the same packets, so a screenshot of
    // a recording is the screenshot of that world, every time: a server that
    // generates more terrain, or a world that is reset, changes nothing.
    std::filesystem::path record;
    std::filesystem::path replay;
};

// Which world the player is in and what shape it has. min_y and height decide
// where a chunk's lowest section sits, which turns every world y into an array
// index - so the overworld's -64 is right in the overworld and nowhere else.
struct Dimension {
    std::string name = "minecraft:overworld";
    std::string type = "minecraft:overworld";
    int32_t min_y = -64;
    int32_t height = 384;
    bool has_skylight = true;
    bool has_ceiling = false;
    float ambient_light = 0.0f;
    int32_t sea_level = 63;
    int32_t section_count() const { return height / 16; }
};

// Everything the server says about another thing in the world, as of the last
// packet about it. Angles are in degrees; velocity is in blocks per tick,
// which is the unit the game moves in.
struct Entity {
    int32_t id = 0;
    uint64_t uuid[2]{};
    int32_t type = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float head_yaw = 0.0f;
    int32_t data = 0;   // means something different for every type that uses it
    bool on_ground = false;
};

struct SessionEvents {
    std::function<void(std::string_view)> on_status;
    std::function<void(std::string_view)> on_chat;
    std::function<void(std::string_view)> on_action_bar;
    std::function<void(std::string_view)> on_disconnect;
    std::function<void(const world::Chunk&)> on_chunk;
    // Where the server says we are. It decides, not us: starting anywhere else
    // makes the first movement packet look like a teleport.
    std::function<void(double, double, double, float, float)> on_position;
    // Other things in the world. type is the registry id from the server's own
    // report; we do not pretend to know what it means yet.
    std::function<void(const Entity&)> on_entity_add;
    std::function<void(const Entity&)> on_entity_move;
    std::function<void(int32_t)> on_entity_remove;
    // A block changed under us. The renderer has to remesh the section it is
    // in, and the sections next to it when it sits on a seam.
    std::function<void(int32_t, int32_t, int32_t, int32_t)> on_block_change;
    // New light for a chunk that is already loaded. Breaking a block changes
    // what the sky reaches, and a chunk meshed against the old light is a
    // chunk with a shadow where there is nothing to cast one.
    std::function<void(int32_t, int32_t, ByteBuffer&)> on_light_update;
    // The server has stopped tracking this chunk for us. Without honouring it
    // nothing is ever let go: every chunk ever seen stays in memory and on the
    // gpu until the reservation is full.
    std::function<void(int32_t, int32_t)> on_chunk_forget;
    // Where the server centres the area it keeps loaded for us, and how far it
    // reaches. A proxy may never send forget_level_chunk; this is how the
    // client knows what to let go of anyway.
    std::function<void(int32_t, int32_t, int32_t)> on_chunk_area;
    // The player moved to another dimension, or respawned. Everything the
    // client held belongs to the world it left: chunks, light, entities.
    std::function<void(const Dimension&)> on_respawn;
};

// One stack in a slot. An empty slot is a count of zero. `readable` goes false
// when the stack carried data components, whose bodies differ per component
// type: the id and count before them are still right, the slots after it in
// the same packet are not.
struct ItemStack {
    int32_t id = 0;
    int32_t count = 0;
    bool readable = true;
};

// The player's own inventory, in the order the server sends it: a crafting
// result, the crafting grid, the four armour slots, the three main rows, the
// hotbar, and the off hand.
struct Inventory {
    static constexpr size_t kSlots = 46;
    static constexpr size_t kHotbarFirst = 36;
    static constexpr size_t kHotbarSlots = 9;
    static constexpr size_t kOffhand = 45;

    std::array<ItemStack, kSlots> slots{};
    ItemStack carried{};
    int32_t held = 0;   // which of the nine the player has in hand

    static constexpr size_t kArmourFirst = 5;   // head, chest, legs, feet
    static constexpr size_t kArmourSlots = 4;

    const ItemStack& hotbar(size_t index) const { return slots[kHotbarFirst + index]; }
};

// Offline mode only: a server in online-mode sends an encryption request, which
// needs a Microsoft session we do not have yet.
class Session {
public:
    Session(SessionOptions options, SessionEvents events);

    void connect();
    bool pump();
    // Lets a render loop drain whatever arrived without blocking on an idle socket.
    bool has_data() const;
    // True between the two delimiters of a bundle. The server bundles what has
    // to land together - a player's spawn with its skin and equipment - and a
    // frame drawn in the middle shows one frame of a naked default player.
    bool in_bundle() const { return in_bundle_; }
    size_t entity_count() const { return entities_.size(); }
    const std::map<int32_t, Entity>& entities() const { return entities_; }
    // What the server says our latency is, or -1 before it has said.
    int32_t ping_ms() const { return ping_ms_; }
    void close();
    // Biomes in the order the server sent them, which is the order a chunk's
    // palette indexes into.
    const world::BiomeTable& biomes() const { return biomes_; }
    // Every registry the server sent in configuration.
    const Registries& registries() const { return registries_; }
    const Dimension& dimension() const { return dimension_; }
    const Inventory& inventory() const { return inventory_; }

    // What the hud shows about the player. Health and food are in half units,
    // the way the game counts hearts and drumsticks.
    struct Vitals {
        float health = 20.0f;
        int32_t food = 20;
        float saturation = 5.0f;
        float experience = 0.0f;   // 0 to 1 across the bar
        int32_t level = 0;
        int32_t total_experience = 0;
        // Survival, creative, adventure, spectator, in the game's own order.
        // Creative and spectator show no health, food or armour at all.
        int32_t game_mode = 0;
        bool survival_bars() const { return game_mode == 0 || game_mode == 2; }
    };
    // What the server lets the player do. Whether flying is allowed at all is
    // its call, not the client's; a survival player who can double tap into
    // the air is a player who is cheating.
    struct Abilities {
        bool invulnerable = false;
        bool flying = false;
        bool may_fly = false;
        bool instant_build = false;
        float flying_speed = 0.05f;
        float walking_speed = 0.1f;
    };
    const Abilities& abilities() const { return abilities_; }
    const Vitals& vitals() const { return vitals_; }
    // Switches the hand locally and tells the server, the way the wheel does.
    void send_held_slot(int32_t slot);
    // One tick of being a player rather than a viewer. Vanilla wants the tick
    // marker before the position, or it ignores the movement entirely.
    void send_position(double x, double y, double z, float yaw, float pitch, bool on_ground);

    // Mining and building. The game numbers every one of these and the server
    // acknowledges the number, which is how it undoes a block the player was
    // not allowed to change without the client having to guess.
    void send_dig(int32_t x, int32_t y, int32_t z, int32_t face, bool finished);
    void send_place(int32_t x, int32_t y, int32_t z, int32_t face, float cursor_x,
                    float cursor_y, float cursor_z);
    void send_swing();

    void send_chat(std::string_view message);
    void send_command(std::string_view command);

    SessionState state() const { return state_; }
    const std::string& profile_name() const { return profile_name_; }
    const std::string& profile_uuid() const { return profile_uuid_; }
    int32_t entity_id() const { return entity_id_; }

private:
    bool light_format_warned_ = false;
    bool slot_format_warned_ = false;
    Inventory inventory_;
    Registries registries_;
    Dimension dimension_;
    void read_spawn_info(ByteBuffer& in);
    Vitals vitals_;
    Abilities abilities_;
    int32_t sequence_ = 0;
    bool announced_loaded_ = false;
    bool in_bundle_ = false;
    std::ofstream recording_;
    std::ifstream playback_;
    bool next_from_playback(PacketConnection::Packet& packet);
    std::set<int32_t> part_read_;
    // Where the player last was, by the server's word or by our own movement:
    // what a relative teleport is an offset from.
    double last_x_ = 0.0;
    double last_y_ = 0.0;
    double last_z_ = 0.0;
    float last_yaw_ = 0.0f;
    float last_pitch_ = 0.0f;
    int32_t area_x_ = 0;
    int32_t area_z_ = 0;
    int32_t area_radius_ = -1;
    bool player_list_broken_ = false;
    int32_t ping_ms_ = -1;

    void read_player_list(ByteBuffer& in);
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
    world::BiomeTable biomes_;
    PacketConnection connection_;
    std::mutex send_mutex_;
    SessionState state_ = SessionState::Handshaking;
    std::string profile_name_;
    std::string profile_uuid_;
    int32_t entity_id_ = -1;
    std::map<int32_t, Entity> entities_;
};

} // namespace mcpp
