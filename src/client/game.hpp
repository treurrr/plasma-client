#pragma once
// The interactive client, split by area. Each area keeps its state in its own
// struct in Game and its work in src/client/<area>.cpp, and play.cpp calls
// every area's hooks in one fixed order. An area adds fields to its own struct
// and code to its own file; play.cpp and the other areas stay as they are.
#include "client/common.hpp"

#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/outline.hpp"
#include "mcpp/gpu/sky.hpp"
#include "mcpp/gpu/text.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/window.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/keybinds.hpp"
#include "mcpp/options.hpp"
#include "mcpp/session.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/world.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/light.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace client {

using ChunkKey = std::pair<int32_t, int32_t>;

struct WorldArea {
    explicit WorldArea(const mcpp::world::BlockTable& table) : light(table) {}

    // The first chunks, fetched before the window opens.
    std::mutex guard;
    std::vector<mcpp::world::Chunk> chunks;
    std::atomic<bool> enough{false};
    std::vector<mcpp::world::Chunk> pending;
    std::set<ChunkKey> seen;
    // Chunks let go while the mesher may still be holding them. It checks this
    // after meshing, so a chunk forgotten mid-flight does not come back.
    std::set<ChunkKey> gone;

    // Kept so the player has something to stand on; the mesher hands them over
    // once it has turned them into triangles.
    std::mutex world_guard;
    std::map<ChunkKey, mcpp::world::Chunk> world;
    mcpp::world::LightEngine light;

    // Chunks whose geometry stopped matching the world. A set, because a wall
    // of changes in one chunk is still one remesh.
    std::set<ChunkKey> dirty;
    std::mutex dirty_guard;
    std::vector<ChunkKey> forgotten;

    // Meshing runs here, never in the frame. The frame only ever does the copy.
    struct Batch {
        std::vector<mcpp::world::SectionMesh> pieces;
        size_t chunks = 0;
    };
    std::vector<Batch> ready;
    std::mutex ready_guard;
    std::atomic<bool> streaming{true};
    std::thread mesher;
    size_t streamed = 0;

    std::vector<mcpp::world::SectionMesh> first_meshes;
    size_t vertex_bytes = 0;
    std::optional<mcpp::gpu::Sky> sky;
    std::optional<mcpp::gpu::Outline> outline;

    // Unloaded counts as solid, so nobody falls out of the world while a chunk
    // is still on its way.
    mcpp::world::BlockAt block_at;
};

struct EntitiesArea {
};

struct PlayerArea {
    double spawn[3] = {0.0, 0.0, 0.0};
    float spawn_yaw = 0.0f;
    float spawn_pitch = 0.0f;
    bool placed = false;

    float eye[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float physics_accumulator = 0.0f;
    float fov = 70.0f;
    bool attacking = false;
    bool using_item = false;
    std::chrono::steady_clock::time_point dig_started;
    mcpp::world::Body body;
    bool flying = false;
    size_t ticks_sent = 0;

    // This frame's view, worked out before the server's packets are applied
    // and acted on after.
    float game_yaw = 0.0f;
    int32_t aimed[3] = {0, 0, 0};
    bool aiming = false;
};

struct ScreensArea {
    ScreensArea() : menu(mcpp::ui::ContainerMenu::player_inventory(&items)) {}

    std::mutex chat_guard;
    std::vector<std::string> chat;
    std::optional<mcpp::gpu::TextRenderer> hud;
    std::optional<mcpp::gpu::UiRenderer> quads;
    bool menu_open = false;
    int menu_filter = -1;
    // Set while any screen has the input; the name is older than the stack.
    bool inventory_open = false;

    mcpp::ui::ScreenManager manager;
    mcpp::Translations lang;
    mcpp::Options options;
    mcpp::Keybinds keybinds;
    mcpp::ui::ItemTable items;
    mcpp::ui::ContainerMenu menu;
    mcpp::ui::World world;
    std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
};

struct SoundArea {
};

struct Game {
    explicit Game(size_t limit) : chunk_limit(limit), world(table) {}

    Address address;
    size_t chunk_limit = 0;
    mcpp::world::BlockTable table;
    mcpp::SessionOptions options;
    std::optional<mcpp::Session> session;

    mcpp::gpu::Device device;
    mcpp::world::Atlas atlas;
    std::optional<mcpp::gpu::Window> window;
    std::optional<mcpp::gpu::WorldRenderer> renderer;
    mcpp::gpu::Camera camera{};

    // This frame: what the player did, how long the last one took, and whether
    // the game's own clock ticks in it.
    mcpp::gpu::FrameInput input;
    std::chrono::steady_clock::time_point now;
    float dt = 0.0f;
    bool tick_due = false;
    double fps = 0.0;

    WorldArea world;
    EntitiesArea entities;
    PlayerArea player;
    ScreensArea screens;
    SoundArea sound;
};

// Opens a window on a server and plays until it is closed.
int play(const std::string& target, size_t chunk_limit);

// Each area's hooks, in the order play.cpp calls them:
//   bind   before connecting: the area hangs its handlers on the session's events
//   start  once the first chunks are in and the window is open
//   input  each frame, on the keys of that frame, before anything moves
//   tick   each frame, after the server's packets of that frame are applied
//   draw   each frame, queueing what the area puts on screen
//   stop   after the last frame, before the session closes
void world_bind(Game& game, mcpp::SessionEvents& events);
void world_start(Game& game);
void world_input(Game& game);
void world_tick(Game& game);
void world_draw(Game& game);
void world_stop(Game& game);

void entities_bind(Game& game, mcpp::SessionEvents& events);
void entities_start(Game& game);
void entities_input(Game& game);
void entities_tick(Game& game);
void entities_draw(Game& game);
void entities_stop(Game& game);

void player_bind(Game& game, mcpp::SessionEvents& events);
void player_start(Game& game);
void player_input(Game& game);
void player_tick(Game& game);
void player_draw(Game& game);
void player_stop(Game& game);

void screens_bind(Game& game, mcpp::SessionEvents& events);
void screens_start(Game& game);
void screens_input(Game& game);
void screens_tick(Game& game);
void screens_draw(Game& game);
void screens_stop(Game& game);

void sound_bind(Game& game, mcpp::SessionEvents& events);
void sound_start(Game& game);
void sound_input(Game& game);
void sound_tick(Game& game);
void sound_draw(Game& game);
void sound_stop(Game& game);

// The hooks that do not fit the common six.
// World: meshes the first chunks, before the gpu exists.
void world_load(Game& game);
// Player: moves and aims before the server's packets are applied, and sends
// the position on the game's tick, last; false when the server is gone.
void player_move(Game& game);
bool player_send(Game& game);
// Screens: records the interface over the finished world.
void screens_record(Game& game, VkCommandBuffer command, uint32_t width, uint32_t height);
// World: a block the player changed, drawn before the server confirms it.
void world_predict_block(Game& game, int32_t x, int32_t y, int32_t z, int32_t state);

} // namespace client
