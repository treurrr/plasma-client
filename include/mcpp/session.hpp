#pragma once
#include "mcpp/auth.hpp"
#include "mcpp/packet_io.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/proto/commands.hpp"
#include "mcpp/proto/metadata.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/registries.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/world/biomes.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/environment.hpp"

#include <array>
#include <chrono>
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
    // What the 1.21.11 dimension type carries besides its shape.
    double coordinate_scale = 1.0;
    int32_t logical_height = 384;
    bool has_fixed_time = false;
    std::string skybox = "overworld";            // overworld, end or none
    std::string cardinal_light = "default";      // default or nether
    std::string infiniburn;
    int32_t monster_spawn_block_light_limit = 0;
    // A superflat world puts its horizon and its void darkness at the bottom.
    bool flat_world = false;
    int64_t hashed_seed = 0;
};

// The two clocks the server keeps: how long the world has existed, which only
// counts up, and the time of day, which /time set moves and the timelines are
// read against. ticking is false when the daylight cycle is stopped.
struct WorldTime {
    int64_t game_time = 0;
    int64_t day_time = 0;
    bool ticking = true;
    // Counts set_time packets, so a client that runs the clock on between
    // them knows when the server has corrected it.
    uint32_t updates = 0;
};

// Rain and thunder as the server last set them, each 0 to 1.
struct Weather {
    bool raining = false;
    float rain_level = 0.0f;
    float thunder_level = 0.0f;
};

// The border the server draws round the world. Diameters in blocks; a border
// that is moving goes from size to target over lerp_ms.
struct WorldBorder {
    double centre_x = 0.0;
    double centre_z = 0.0;
    double size = 59999968.0;
    double target = 59999968.0;
    int64_t lerp_ms = 0;
    int32_t absolute_max = 29999984;
    int32_t warning_blocks = 5;
    int32_t warning_seconds = 15;
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
    // What set_entity_data said, merged over every update since add_entity.
    proto::Metadata metadata;
    // What it rides and what rides it, by network id; -1 for nothing.
    int32_t vehicle = -1;
    std::vector<int32_t> passengers;
    int32_t leashed_to = -1;
};

struct EntityMove {
    int32_t id = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool has_position = false;
    bool has_rotation = false;
    // entity_position_sync or teleport_entity: snapped instead when far.
    bool absolute = false;
};

// One line of the player list. The list is what turns a player entity's uuid
// into a name and a skin: add_entity carries neither.
struct PlayerInfo {
    struct Property {
        std::string name;
        std::string value;
        std::string signature;
    };
    std::string uuid;
    std::string name;
    // "textures" holds the skin: base64 of a JSON document naming its url.
    std::vector<Property> properties;
    bool chat_session = false;
    int32_t game_mode = 0;
    bool listed = false;
    int32_t latency = 0;
    std::string display_name;   // empty when the server set none
    int32_t list_priority = 0;
    bool show_hat = true;

    const Property* property(std::string_view wanted) const {
        for (const auto& p : properties) if (p.name == wanted) return &p;
        return nullptr;
    }
};

// The uuid of an entity in the form the player list uses.
std::string uuid_string(const uint64_t uuid[2]);

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
        // A game_event the world area does not act on itself, as the server
        // numbered it: a win screen, a demo message, an arrow hitting a player.
        std::function<void(int32_t, float)> on_game_event;
        // New biomes for chunks that are already loaded; the chunk keeps its
        // blocks and must be meshed again for its tints.
        std::function<void(int32_t, int32_t, std::vector<world::PalettedContainer>&)>
            on_chunk_biomes;
        // A block entity's data, from the chunk or on its own: x, y, z, type
        // and its compound.
        std::function<void(int32_t, int32_t, int32_t, int32_t, const nbt::Value&)>
            on_block_entity;
        // The world border: centre, diameter now, diameter it is moving to and
        // in how many milliseconds, and the two warning settings.
        std::function<void(const WorldBorder&)> on_border;
    } world;

    struct Entities {
        // Other things in the world. type is the registry id from the server's own
        // report; we do not pretend to know what it means yet.
        std::function<void(const Entity&)> on_add;
        std::function<void(const Entity&)> on_move;
        std::function<void(int32_t)> on_remove;
        // A player joined the list or a line of it changed; and one left it.
        std::function<void(const PlayerInfo&)> on_player_info;
        std::function<void(std::string_view)> on_player_remove;
        // One status byte: hurt, death, a totem, a sheep eating, sixty-odd more.
        std::function<void(int32_t, uint8_t)> on_event;
        // An arm swing, a wake-up, a critical hit's sparks.
        std::function<void(int32_t, uint8_t)> on_animate;
        // The red flash, with the direction the hit came from.
        std::function<void(int32_t, float)> on_hurt;
        // Who rides whom changed; the entity is the vehicle.
        std::function<void(const Entity&)> on_passengers;
        // A movement as the packet put it: which of position and angles it
        // carried, and whether it was a sync or teleport rather than a step.
        std::function<void(const EntityMove&)> on_moved;
        // rotate_head: the head's own yaw, eased in apart from the body.
        std::function<void(int32_t, float)> on_head;
        // damage_event: the walk cycle kicks and the entity flashes red.
        std::function<void(int32_t)> on_damage;
    } entities;

    struct Player {
        // Where the server says we are. It decides, not us: starting anywhere else
        // makes the first movement packet look like a teleport.
        std::function<void(double, double, double, float, float)> on_position;
        // The teleport as the server sent it, for a client that moves itself:
        // it resolves the relative bits against its own body and answers with
        // accept_teleport. Without a handler the session answers by itself.
        struct Teleport {
            int32_t id = 0;
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            double vx = 0.0;
            double vy = 0.0;
            double vz = 0.0;
            float yaw = 0.0f;
            float pitch = 0.0f;
            // x, y, z, yaw, pitch, then velocity x, y, z, then turning the
            // velocity with the change of facing.
            uint32_t relative = 0;
        };
        std::function<void(const Teleport&)> on_teleport;
        // The server setting our own velocity: knockback, a launch. The game
        // takes it as the body's new delta movement.
        std::function<void(double, double, double)> on_motion;
        // B: the server has applied every block action up to this sequence.
        std::function<void(int32_t)> on_block_changed_ack;
        // B: someone's crack in a block, by breaker id; outside 0..9 it stops.
        std::function<void(int32_t, int32_t, int32_t, int32_t, int32_t)> on_block_destruction;
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
        // A sound the server plays: at a place, or on an entity it follows.
        // The event is the registry's name, or the one the server wrote out
        // inline with its own range; the seed picks the variant.
        struct Cue {
            std::string event;
            bool has_fixed_range = false;
            float fixed_range = 0.0f;
            int32_t category = 0;
            int32_t entity = -1;   // -1 for one at a place
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            float volume = 1.0f;
            float pitch = 1.0f;
            int64_t seed = 0;
        };
        std::function<void(const Cue&)> on_sound;
        // Category -1 and an empty name each mean all of them.
        std::function<void(int32_t, std::string_view)> on_stop;
        // The numbered effects the client plays from its own tables: a block
        // breaking, an anvil landing, a record starting. Particles hang here too.
        std::function<void(int32_t, int32_t, int32_t, int32_t, int32_t, bool)> on_level_event;
    } sound;

    struct Data {
        // set_entity_data changed something on this entity, the player included.
        std::function<void(int32_t)> on_entity_data;
        // A plugin channel message the session does not answer itself.
        std::function<void(std::string_view, const std::vector<uint8_t>&)> on_custom_payload;
        // New biomes for a chunk already loaded, one container per section from
        // the bottom up: remesh it without new blocks.
        std::function<void(int32_t, int32_t, const std::vector<world::PalettedContainer>&)>
            on_chunk_biomes;
        // A configuration round cleared chat and the signature chain with it.
        std::function<void()> on_reset_chat;
        // The server moved us to another one; the session has reconnected there.
        std::function<void(std::string_view, uint16_t)> on_transfer;
    } data;
};

// One stack in a slot. An empty slot is a count of zero. `readable` goes false
// only when a component could not be decoded, which loses the slots after it
// in the same packet.
struct ItemStack {
    int32_t id = 0;
    int32_t count = 0;
    bool readable = true;
    proto::Components components;
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
    // The sky, fog and light the server's data describes, for the dimension
    // the player is in.
    const world::Environment& environment() const { return world_.environment; }
    const WorldTime& time() const { return world_.time; }
    const Weather& weather() const { return world_.weather; }
    const WorldBorder& border() const { return world_.border; }

    // Entities: session_entities.cpp.
    // True between the two delimiters of a bundle. The server bundles what has
    // to land together - a player's spawn with its skin and equipment - and a
    // frame drawn in the middle shows one frame of a naked default player.
    bool in_bundle() const { return entities_.in_bundle; }
    size_t entity_count() const { return entities_.entities.size(); }
    const std::map<int32_t, Entity>& entities() const { return entities_.entities; }
    // What the server says our latency is, or -1 before it has said.
    int32_t ping_ms() const { return entities_.ping_ms; }
    // Everyone on the player list, by uuid, listed or not.
    const std::map<std::string, PlayerInfo, std::less<>>& players() const {
        return entities_.players;
    }
    const PlayerInfo* player(std::string_view uuid) const {
        const auto found = entities_.players.find(uuid);
        return found == entities_.players.end() ? nullptr : &found->second;
    }
    // The movement keys and sneak, as a held state rather than a press: the
    // server decides from these whether the player is crouching. Bits in the
    // game's order: forward, back, left, right, jump, sneak, sprint.
    void send_input_state(uint8_t keys);
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
    // Start (0), abort (1) or finish (2) breaking a block, on a face from 0
    // (down) to 5 (east). Start and finish are predicted and numbered.
    void send_dig_action(int32_t action, int32_t x, int32_t y, int32_t z, int32_t face);
    // B: the sequence number the last predicted action went out under.
    int32_t sequence() const { return player_.sequence; }
    void send_place(int32_t x, int32_t y, int32_t z, int32_t face, float cursor_x,
                    float cursor_y, float cursor_z);
    void send_swing();

    // One game tick of a player that moves itself, in the order the game
    // sends it: the keys if they changed, sprinting if it changed, the
    // cheapest of the four movement packets that says what changed, and the
    // end of the tick. The rotation is the game's: yaw 0 faces south.
    struct Movement {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool on_ground = false;
        bool horizontal_collision = false;
        bool sprinting = false;
        // The keys held: forward, back, left, right, jump, sneak, sprint.
        uint8_t keys = 0;
    };
    void send_movement(const Movement& movement);
    // The game's last word in each tick, after everything else it sent.
    void send_tick_end();
    // Creative flight switched on or off by the player.
    void send_flying(bool flying);
    // The answer to a teleport the client resolved itself: where it now is.
    void accept_teleport(int32_t id, double x, double y, double z, float yaw, float pitch);
    // How many times the server has put us somewhere we did not move to,
    // after the first placement: corrections, as a test counts them.
    size_t teleports() const { return player_.teleports; }
    // The level of an effect on the player by name, 0 when it is not on.
    int32_t effect_level(std::string_view name) const;

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
    // An entity's data, or the player's own for entity_id(); empty for one the
    // server has said nothing about.
    const proto::Metadata& metadata(int32_t entity) const;
    // A container's properties from container_set_data, by property index:
    // a furnace's fuel and progress, an enchanting table's offers.
    const std::map<int32_t, int32_t>& container_properties(int32_t container) const;
    const proto::CommandTree& commands() const { return data_.commands; }
    // update_recipes, every recipe_book_add since the last replacing one, and
    // the book's open and filter switches, decoded by the layout table.
    const proto::Value& recipe_sets() const { return data_.recipe_sets; }
    const std::vector<proto::Value>& recipe_book() const { return data_.recipe_book; }
    const proto::Value& recipe_book_settings() const { return data_.recipe_book_settings; }
    // What the server says about itself in play, its pause menu links and the
    // lines it wants in a crash report.
    const proto::Value& server_data() const { return data_.server_data; }
    const proto::Value& server_links() const { return data_.server_links; }
    const proto::Value& report_details() const { return data_.report_details; }
    const std::string& server_brand() const { return data_.server_brand; }
    // The feature flags the server has on: minecraft:vanilla, and any
    // experiment that gates its own items and blocks.
    const std::vector<std::string>& enabled_features() const { return data_.features; }
    // Ticks per second, and whether the server has frozen time; a frozen
    // world moves nothing forward but by ticking_step.
    float tick_rate() const { return data_.tick_rate; }
    bool tick_frozen() const { return data_.tick_frozen; }
    int32_t tick_steps() const { return data_.tick_steps; }
    // How much of an item group's cooldown is left, 1 just after it began and
    // 0 when it is over, at the server's tick rate.
    float cooldown_left(std::string_view group) const;

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
        world::Environment environment;
        bool environment_loaded = false;
        WorldTime time;
        Weather weather;
        WorldBorder border;
    };
    WorldState world_;
    void read_spawn_info(ByteBuffer& in);
    void read_biome_extras();

    struct EntityState {
        std::map<int32_t, Entity> entities;
        std::map<int32_t, std::map<int32_t, MobEffect>> effects;
        std::map<int32_t, std::map<std::string, Attribute, std::less<>>> attributes;
        bool in_bundle = false;
        bool player_list_broken = false;
        int32_t ping_ms = -1;
        std::map<std::string, PlayerInfo, std::less<>> players;
        // Where relative moves count from: the last absolute or relative
        // position the server sent, not where a teleport put the entity.
        std::map<int32_t, std::array<double, 3>> move_bases;
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
        // What the server was last told, so each tick sends only what changed.
        double sent_x = 0.0;
        double sent_y = 0.0;
        double sent_z = 0.0;
        float sent_yaw = 0.0f;
        float sent_pitch = 0.0f;
        bool sent_on_ground = false;
        bool sent_collision = false;
        bool sent_sprinting = false;
        uint8_t sent_keys = 0;
        int32_t position_reminder = 0;
        size_t teleports = 0;
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
        proto::Metadata own_metadata;
        std::map<int32_t, std::map<int32_t, int32_t>> container_properties;
        proto::CommandTree commands;
        proto::Value recipe_sets;
        std::vector<proto::Value> recipe_book;
        proto::Value recipe_book_settings;
        proto::Value server_data;
        proto::Value server_links;
        proto::Value report_details;
        std::string server_brand;
        std::vector<std::string> features;
        float tick_rate = 20.0f;
        bool tick_frozen = false;
        int32_t tick_steps = 0;
        struct Cooldown {
            std::chrono::steady_clock::time_point start;
            int32_t ticks = 0;
        };
        std::map<std::string, Cooldown, std::less<>> cooldowns;
        // Cookies live as long as the client does, across transfers.
        std::map<std::string, std::vector<uint8_t>, std::less<>> cookies;
        std::set<int32_t> warned;
        bool brand_sent = false;
    };
    DataState data_;
    void read_tags(ByteBuffer& in);
    // Configuration and play share these packets, with their own ids.
    bool handle_data_common(PacketConnection::Packet& packet, bool configuration);
    void transfer(std::string host, uint16_t port);
    void send_brand();
    // The handshake's intent: login, or a transfer from another server.
    int32_t intent_ = 2;

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
