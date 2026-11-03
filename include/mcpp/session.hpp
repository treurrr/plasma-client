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
#include <vector>

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

// A potion effect on something, as the server last described it. Duration is
// in ticks, -1 for one that never runs out.
struct MobEffect {
    int32_t id = 0;
    int32_t amplifier = 0;   // 0 is level I
    int32_t duration = 0;
    bool ambient = false;    // from a beacon or a conduit
    bool particles = true;
    bool icon = true;
    bool blend = false;
};

// One attribute of something - its speed, its reach, its health - with every
// modifier the server put on it.
struct Attribute {
    struct Modifier {
        std::string id;
        double amount = 0.0;
        // Adds, adds that many times the base, or multiplies the result.
        int32_t operation = 0;
    };
    double base = 0.0;
    std::vector<Modifier> modifiers;

    // Additions first, then the multiples of what they summed to, then each
    // multiplier on the whole: the game's own order.
    double value() const {
        double sum = base;
        for (const auto& m : modifiers) if (m.operation == 0) sum += m.amount;
        double result = sum;
        for (const auto& m : modifiers) if (m.operation == 1) result += sum * m.amount;
        for (const auto& m : modifiers) if (m.operation == 2) result *= 1.0 + m.amount;
        return result;
    }
};

// What the hud is told, kept as the components the server sent so colours and
// translations survive to the screen.
//
// One boss_event: which bar, what happened to it, and the fields that
// operation carries.
struct BossEvent {
    enum class Op : uint8_t { Add, Remove, Progress, Name, Style, Flags };
    std::string uuid;
    Op op = Op::Add;
    nbt::Value name;
    float progress = 0.0f;
    // Pink, blue, red, green, yellow, purple, white; and progress, notched 6,
    // 10, 12, 20: the game's own orders.
    int32_t colour = 0;
    int32_t overlay = 0;
    // Darken the sky, play the boss music, thicken the fog.
    uint8_t flags = 0;
};

// The scoreboard as set_objective, set_score, reset_score,
// set_display_objective and set_player_team leave it.
struct Scoreboard {
    // How a score is written: the place's default, nothing, in a style, or a
    // fixed text in place of the number.
    struct NumberFormat {
        enum class Kind : uint8_t { Default, Blank, Styled, Fixed };
        Kind kind = Kind::Default;
        nbt::Value value;
    };
    struct Objective {
        nbt::Value display_name;
        bool hearts = false;
        NumberFormat format;
    };
    struct Score {
        int32_t value = 0;
        nbt::Value display_name;   // null: the owner's own name
        NumberFormat format;
    };
    struct Team {
        nbt::Value display_name;
        nbt::Value prefix;
        nbt::Value suffix;
        uint8_t options = 0;   // friendly fire, see invisible team mates
        int32_t nametag_visibility = 0;
        int32_t collision_rule = 0;
        int32_t colour = 21;   // the game's formatting codes in order; 21 is none
    };
    // The list, the sidebar, below names, then the sidebar a team of each of
    // the sixteen colours sees instead.
    static constexpr size_t kSlots = 19;
    static constexpr size_t kList = 0;
    static constexpr size_t kSidebar = 1;
    static constexpr size_t kBelowName = 2;

    std::map<std::string, Objective, std::less<>> objectives;
    // By owner, then by objective.
    std::map<std::string, std::map<std::string, Score, std::less<>>, std::less<>> scores;
    std::array<std::string, kSlots> display;
    std::map<std::string, Team, std::less<>> teams;
    std::map<std::string, std::string, std::less<>> team_of;

    const Team* team_for(std::string_view owner) const {
        const auto found = team_of.find(owner);
        if (found == team_of.end()) return nullptr;
        const auto team = teams.find(found->second);
        return team == teams.end() ? nullptr : &team->second;
    }
    const Objective* displayed(size_t slot) const {
        if (slot >= kSlots || display[slot].empty()) return nullptr;
        const auto found = objectives.find(display[slot]);
        return found == objectives.end() ? nullptr : &found->second;
    }
};

// What the session tells the client, one struct per area of the game. An area
// adds its events to its own struct, so two of them never edit the same lines.
struct SessionEvents {
    std::function<void(std::string_view)> on_status;
    std::function<void(std::string_view)> on_disconnect;

    struct World {
        std::function<void(const world::Chunk&)> on_chunk;
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
    } world;

    struct Entities {
        // Other things in the world. type is the registry id from the server's own
        // report; we do not pretend to know what it means yet.
        std::function<void(const Entity&)> on_add;
        std::function<void(const Entity&)> on_move;
        std::function<void(int32_t)> on_remove;
    } entities;

    struct Player {
        // Where the server says we are. It decides, not us: starting anywhere else
        // makes the first movement packet look like a teleport.
        std::function<void(double, double, double, float, float)> on_position;
    } player;

    struct Ui {
        std::function<void(std::string_view)> on_chat;
        std::function<void(std::string_view)> on_action_bar;
        // The server shut a container, ours included.
        std::function<void(int32_t)> on_container_close;
        // We died, and this is what killed us, as the component the server sent.
        std::function<void(const nbt::Value&)> on_combat_kill;
        // The links the pause screen offers: a label (a component, or one of
        // the game's built-in kinds by number) and an address.
        std::function<void(const std::vector<std::pair<nbt::Value, std::string>>&)> on_server_links;
        // H: a chat line as its component, with the tag the game draws beside
        // it: 0 none, 1 from the server itself, 2 an unsigned player message.
        std::function<void(const nbt::Value&, int32_t)> on_chat_component;
        // H: the line over the hotbar, from set_action_bar_text or system_chat.
        std::function<void(const nbt::Value&)> on_overlay_message;
        std::function<void(const nbt::Value&)> on_title;
        std::function<void(const nbt::Value&)> on_subtitle;
        // Fade in, stay, fade out, in ticks.
        std::function<void(int32_t, int32_t, int32_t)> on_title_times;
        // True also puts the times back to the game's defaults.
        std::function<void(bool)> on_clear_titles;
        std::function<void(const BossEvent&)> on_boss_event;
        // The player list's header and footer.
        std::function<void(const nbt::Value&, const nbt::Value&)> on_tab_list;
    } ui;

    struct Sound {
    } sound;

    struct Data {
    } data;
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
//
// The connection, login and configuration live in session.cpp. The play state
// is split by area: each area handles its own packets in src/session_<area>.cpp
// and keeps its own state in its own struct below, and handle_play asks each
// area in a fixed order.
class Session {
public:
    Session(SessionOptions options, SessionEvents events);

    void connect();
    bool pump();
    // Lets a render loop drain whatever arrived without blocking on an idle socket.
    bool has_data() const;
    void close();

    SessionState state() const { return state_; }
    const std::string& profile_name() const { return profile_name_; }
    const std::string& profile_uuid() const { return profile_uuid_; }
    int32_t entity_id() const { return entity_id_; }

    // World: session_world.cpp.
    // Biomes in the order the server sent them, which is the order a chunk's
    // palette indexes into.
    const world::BiomeTable& biomes() const { return world_.biomes; }
    const Dimension& dimension() const { return world_.dimension; }

    // Entities: session_entities.cpp.
    // True between the two delimiters of a bundle. The server bundles what has
    // to land together - a player's spawn with its skin and equipment - and a
    // frame drawn in the middle shows one frame of a naked default player.
    bool in_bundle() const { return entities_.in_bundle; }
    size_t entity_count() const { return entities_.entities.size(); }
    const std::map<int32_t, Entity>& entities() const { return entities_.entities; }
    // What the server says our latency is, or -1 before it has said.
    int32_t ping_ms() const { return entities_.ping_ms; }
    // What the server put on an entity, ours included; empty for one it has
    // said nothing about.
    const std::map<int32_t, MobEffect>& effects(int32_t entity) const;
    const std::map<std::string, Attribute, std::less<>>& attributes(int32_t entity) const;
    // An attribute's value, or the fallback when the server never sent it.
    double attribute(int32_t entity, std::string_view name, double fallback) const;

    // Player: session_player.cpp.
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
    const Inventory& inventory() const { return player_.inventory; }
    const Vitals& vitals() const { return player_.vitals; }
    const Abilities& abilities() const { return player_.abilities; }
    bool hardcore() const { return player_.hardcore; }
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

    // Ui: session_ui.cpp.
    void send_chat(std::string_view message);
    void send_command(std::string_view command);
    // A click in a container: what the client already did to its own copy,
    // the slots that changed and what the cursor now holds, so the server can
    // check it rather than replay it.
    void send_container_click(int32_t container, int16_t slot, int8_t button, int32_t mode,
                              const std::vector<std::pair<int16_t, ItemStack>>& changed,
                              const ItemStack& carried);
    void send_container_close(int32_t container);
    // Creative mode: the client says what a slot of its own inventory holds.
    void send_creative_slot(int16_t slot, const ItemStack& stack);
    // The client's own idea of the inventory after a click, until the server
    // confirms or corrects it.
    void predict_inventory(const std::array<ItemStack, Inventory::kSlots>& slots,
                           const ItemStack& carried);
    // The state id the server last gave the open container.
    int32_t container_state() const { return player_.container_state; }
    void send_respawn();
    // H: the scoreboard as the server last described it.
    const Scoreboard& scoreboard() const { return ui_.scoreboard; }

    // Data: session_data.cpp.
    // Every registry the server sent in configuration.
    const Registries& registries() const { return data_.registries; }
    const Tags& tags() const { return data_.tags; }
    // Peaceful, easy, normal, hard.
    int32_t difficulty() const { return data_.difficulty; }
    bool difficulty_locked() const { return data_.difficulty_locked; }

private:
    // Each returns whether the packet was its area's. A new packet goes in the
    // area it belongs to; handle_play itself only knows the connection's own.
    bool handle_world(PacketConnection::Packet& packet);
    bool handle_entities(PacketConnection::Packet& packet);
    bool handle_player(PacketConnection::Packet& packet);
    bool handle_ui(PacketConnection::Packet& packet);
    bool handle_sound(PacketConnection::Packet& packet);
    bool handle_data(PacketConnection::Packet& packet);
    // H: the chat packets once more, as components, before handle_ui reads them.
    void peek_chat_components(PacketConnection::Packet& packet);

    struct WorldState {
        Dimension dimension;
        world::BiomeTable biomes;
        bool light_format_warned = false;
        int32_t area_x = 0;
        int32_t area_z = 0;
        int32_t area_radius = -1;
    };
    WorldState world_;
    void read_spawn_info(ByteBuffer& in);

    struct EntityState {
        std::map<int32_t, Entity> entities;
        std::map<int32_t, std::map<int32_t, MobEffect>> effects;
        std::map<int32_t, std::map<std::string, Attribute, std::less<>>> attributes;
        bool in_bundle = false;
        bool player_list_broken = false;
        int32_t ping_ms = -1;
    };
    EntityState entities_;
    void read_player_list(ByteBuffer& in);

    struct PlayerState {
        Inventory inventory;
        Vitals vitals;
        Abilities abilities;
        bool hardcore = false;
        bool slot_format_warned = false;
        int32_t sequence = 0;
        bool announced_loaded = false;
        // Where the player last was, by the server's word or by our own movement:
        // what a relative teleport is an offset from.
        double last_x = 0.0;
        double last_y = 0.0;
        double last_z = 0.0;
        float last_yaw = 0.0f;
        float last_pitch = 0.0f;
        int32_t container_state = 0;
    };
    PlayerState player_;

    struct UiState {
        Scoreboard scoreboard;
    };
    [[maybe_unused]] UiState ui_;

    struct SoundState {
    };
    [[maybe_unused]] SoundState sound_;

    struct DataState {
        Registries registries;
        Tags tags;
        int32_t difficulty = 2;
        bool difficulty_locked = false;
    };
    DataState data_;
    void read_tags(ByteBuffer& in);

    std::ofstream recording_;
    std::ifstream playback_;
    bool next_from_playback(PacketConnection::Packet& packet);
    std::set<int32_t> part_read_;

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
