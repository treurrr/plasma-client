#pragma once
// Digging, the way the game's client does it: when a block starts, carries on
// and stops breaking, how fast the held item gets through it, what goes to
// the server and with which sequence number, and how a broken block shown
// before the server agreed is put back when it does not. The window and the
// headless digtest share it.
#include "client/player_rules.hpp"

#include "mcpp/session.hpp"
#include "mcpp/world/shapes.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace client {

// Every tool's rules by item, from tools-<protocol>.txt (tools/bake_states.py):
// what the item does without a patch to its tool component.
class ToolTable {
public:
    struct Rule {
        std::vector<std::string> blocks;   // "#tag" or block names
        std::vector<int32_t> ids;          // block registry ids
        bool has_speed = false;
        float speed = 1.0f;
        int8_t correct = -1;               // -1 unsaid, 0 no, 1 yes
    };
    struct Tool {
        float default_speed = 1.0f;
        bool creative_breaks = true;
        std::vector<Rule> rules;
    };
    bool open(const std::filesystem::path& path);
    const Tool* find(int32_t item) const;
    static std::filesystem::path default_path(int32_t protocol);
    static const ToolTable& game();

private:
    std::map<int32_t, Tool> tools_;
};

// The held item's speed on a block and whether it gets the drop.
struct ToolOnBlock {
    float speed = 1.0f;
    bool correct = false;
    bool creative_breaks = true;
};
ToolOnBlock tool_on_block(const ToolTable::Tool* tool, int32_t state, const mcpp::world::BlockShapes& shapes,
                          const mcpp::Tags& tags);
// `ItemStack.isSameItemSameComponents`: the same item with the same patch.
bool same_item_same_components(const mcpp::ItemStack& a, const mcpp::ItemStack& b);

// `BlockStatePredictionHandler`: the blocks changed ahead of the server, with
// what the server last said they were, until it acknowledges the sequence.
class BlockPrediction {
public:
    struct Verified {
        int32_t sequence = 0;
        int32_t state = 0;
        double player[3] = {0.0, 0.0, 0.0};
    };
    void retain(const int32_t pos[3], int32_t state, const double player[3]);
    bool update(const int32_t pos[3], int32_t state);
    // Every prediction up to the sequence, handed back in no particular order.
    std::vector<std::pair<std::array<int32_t, 3>, Verified>> end_up_to(int32_t sequence);
    void start(int32_t sequence) {
        current_ = sequence;
        predicting_ = true;
    }
    void stop() { predicting_ = false; }
    bool predicting() const { return predicting_; }
    size_t size() const { return verified_.size(); }

private:
    std::map<std::array<int32_t, 3>, Verified> verified_;
    int32_t current_ = 0;
    bool predicting_ = false;
};

// What the game mode asks of the level and the player.
struct GameModeLevel {
    std::function<int32_t(const int32_t*)> state_at;
    // `Level.setBlock` on the client; true when the block changed.
    std::function<bool(const int32_t*, int32_t)> set_block;
    // `LevelEventHandler.levelEvent` 2001 for our own break: sound and bits.
    std::function<void(const int32_t*, int32_t)> break_effect;
    // `ClientLevel.destroyBlockProgress` for the local player.
    std::function<void(const int32_t*, int32_t)> progress;
    // The hit sound every fourth tick of digging.
    std::function<void(const int32_t*, int32_t)> hit_sound;
    // How far one tick gets through the state with what is held now.
    std::function<float(int32_t)> destroy_progress;
    std::function<const mcpp::ItemStack&()> held;
    // Where the player stands, for a prediction to be put back to.
    std::function<void(double*)> player_position;
};

class MultiPlayerGameMode {
public:
    MultiPlayerGameMode(mcpp::Session& session, BlockPrediction& prediction, GameModeLevel level)
        : session_(session), prediction_(prediction), level_(std::move(level)) {}

    bool destroy_block(const int32_t pos[3]);
    bool start_destroy_block(const int32_t pos[3], int32_t face);
    void stop_destroy_block();
    bool continue_destroy_block(const int32_t pos[3], int32_t face);
    int32_t destroy_stage() const;
    bool destroying() const { return destroying_; }
    bool has_miss_time() const { return session_.vitals().game_mode != 1; }
    bool spectator() const { return session_.vitals().game_mode == 3; }

    // The operator level the server gave us (entity events 24 to 28).
    int32_t permission_level = 0;

    float progress() const { return destroy_progress_; }
    float ticks() const { return destroy_ticks_; }
    int32_t delay() const { return destroy_delay_; }

private:
    bool restricted() const;
    bool same_destroy_target(const int32_t pos[3]) const;
    template <typename Predict> void start_prediction(Predict&& predict);

    mcpp::Session& session_;
    BlockPrediction& prediction_;
    GameModeLevel level_;
    int32_t destroy_pos_[3] = {-1, -1, -1};
    mcpp::ItemStack destroying_item_;
    float destroy_progress_ = 0.0f;
    float destroy_ticks_ = 0.0f;
    int32_t destroy_delay_ = 0;
    bool destroying_ = false;
};

// Everything the destroy speed reads off the player and the held item now.
DigContext dig_context(const mcpp::Session& session, const mcpp::world::Body& body,
                       const ToolOnBlock& tool);

// Joins as PlasmaB2 and breaks stone by hand, with pickaxes and in creative
// on a platform far from spawn, the server as referee.
int digtest2(const std::string& target);

} // namespace client
