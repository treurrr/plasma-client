#include "client/game_mode.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/proto/components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace client {
namespace {

using mcpp::world::BlockShapes;

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, by)) out.push_back(part);
    return out;
}

// A stack's patch as text, which is enough to tell two patches apart.
std::string patch_of(const mcpp::ItemStack& stack) {
    std::string out;
    for (const auto& entry : stack.components.added()) {
        out += std::format("+{}={};", entry.type, mcpp::proto::describe(entry.value));
    }
    for (const int32_t type : stack.components.removed()) out += std::format("-{};", type);
    return out;
}

// The tool component a stack's patch puts in place of its item's own.
std::optional<ToolTable::Tool> patched_tool(const mcpp::ItemStack& stack, bool& removed) {
    const int32_t protocol = mcpp::protocol::kTargetProtocol;
    removed = stack.components.removes(mcpp::proto::component_type("minecraft:tool", protocol));
    const auto* value = stack.components.find("minecraft:tool", protocol);
    if (value == nullptr) return std::nullopt;
    ToolTable::Tool tool;
    if (const auto* speed = value->field("default_mining_speed")) {
        tool.default_speed = static_cast<float>(speed->as_double().value_or(1.0));
    }
    if (const auto* creative = value->field("can_destroy_blocks_in_creative")) {
        tool.creative_breaks = creative->as_bool().value_or(true);
    }
    if (const auto* rules = value->field("rules"); rules != nullptr && rules->as_list() != nullptr) {
        for (const auto& rule_value : *rules->as_list()) {
            ToolTable::Rule rule;
            if (const auto* blocks = rule_value.field("blocks")) {
                if (const auto* tag = blocks->as_string()) {
                    rule.blocks.push_back(*tag);
                } else if (const auto* ids = blocks->as_list()) {
                    for (const auto& id : *ids) rule.ids.push_back(static_cast<int32_t>(id.as_int().value_or(-1)));
                }
            }
            if (const auto* speed = rule_value.field("speed"); speed != nullptr && !speed->empty()) {
                rule.has_speed = true;
                rule.speed = static_cast<float>(speed->as_double().value_or(1.0));
            }
            if (const auto* correct = rule_value.field("correct_for_drops"); correct != nullptr && !correct->empty()) {
                rule.correct = correct->as_bool().value_or(false) ? 1 : 0;
            }
            tool.rules.push_back(std::move(rule));
        }
    }
    return tool;
}

} // namespace

std::filesystem::path ToolTable::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("tools-{}.txt", protocol);
}

const ToolTable& ToolTable::game() {
    static const ToolTable table = [] {
        ToolTable t;
        t.open(default_path(mcpp::protocol::kTargetProtocol));
        return t;
    }();
    return table;
}

bool ToolTable::open(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto columns = split(line, '\t');
        if (columns.size() < 5) continue;
        Tool& tool = tools_[std::stoi(columns[0])];
        if (columns[2] == "*") {
            tool.default_speed = std::stof(columns[3]);
            tool.creative_breaks = columns.size() < 6 || columns[5] != "0";
            continue;
        }
        Rule rule;
        rule.blocks = split(columns[2], ',');
        rule.has_speed = columns[3] != "-";
        if (rule.has_speed) rule.speed = std::stof(columns[3]);
        rule.correct = columns[4] == "-" ? -1 : columns[4] == "1" ? 1 : 0;
        tool.rules.push_back(std::move(rule));
    }
    return !tools_.empty();
}

const ToolTable::Tool* ToolTable::find(int32_t item) const {
    const auto found = tools_.find(item);
    return found == tools_.end() ? nullptr : &found->second;
}

// `Tool.getMiningSpeed`, `Tool.isCorrectForDrops`, `Item.getDestroySpeed`, `Item.isCorrectToolForDrops`: the first rule that names the block decides.
ToolOnBlock tool_on_block(const ToolTable::Tool* tool, int32_t state, const BlockShapes& shapes,
                          const mcpp::Tags& tags) {
    ToolOnBlock out;
    if (tool == nullptr) return out;
    const auto& block = shapes.block(state);
    // `BlockBehaviour.BlockStateBase.is`: the block in a holder set.
    const auto is = [&](const ToolTable::Rule& rule) {
        for (const auto& name : rule.blocks) {
            if (name.starts_with('#')) {
                if (tags.contains("minecraft:block", std::string_view(name).substr(1), block.registry_id)) return true;
            } else if (name == block.name) {
                return true;
            }
        }
        return std::ranges::find(rule.ids, block.registry_id) != rule.ids.end();
    };
    out.speed = tool->default_speed;
    for (const auto& rule : tool->rules) {
        if (rule.has_speed && is(rule)) {
            out.speed = rule.speed;
            break;
        }
    }
    for (const auto& rule : tool->rules) {
        if (rule.correct >= 0 && is(rule)) {
            out.correct = rule.correct == 1;
            break;
        }
    }
    out.creative_breaks = tool->creative_breaks;
    return out;
}

// `ItemStack.isSameItemSameComponents`: same item, same patch.
bool same_item_same_components(const mcpp::ItemStack& a, const mcpp::ItemStack& b) {
    const bool a_empty = a.count <= 0 || a.id == 0;
    const bool b_empty = b.count <= 0 || b.id == 0;
    if (a_empty || b_empty) return a_empty == b_empty;
    return a.id == b.id && patch_of(a) == patch_of(b);
}

// `BlockStatePredictionHandler.retainKnownServerState`: what the server said, kept under the newest sequence.
void BlockPrediction::retain(const int32_t pos[3], int32_t state, const double player[3]) {
    const std::array<int32_t, 3> key{pos[0], pos[1], pos[2]};
    const auto found = verified_.find(key);
    if (found != verified_.end()) {
        found->second.sequence = current_;
        return;
    }
    Verified v;
    v.sequence = current_;
    v.state = state;
    std::copy(player, player + 3, v.player);
    verified_.emplace(key, v);
}

// `BlockStatePredictionHandler.updateKnownServerState`: a server change held back while ours stands.
bool BlockPrediction::update(const int32_t pos[3], int32_t state) {
    const auto found = verified_.find({pos[0], pos[1], pos[2]});
    if (found == verified_.end()) return false;
    found->second.state = state;
    return true;
}

// `BlockStatePredictionHandler.endPredictionsUpTo`: the predictions the server has now answered.
std::vector<std::pair<std::array<int32_t, 3>, BlockPrediction::Verified>> BlockPrediction::end_up_to(
    int32_t sequence) {
    std::vector<std::pair<std::array<int32_t, 3>, Verified>> out;
    for (auto it = verified_.begin(); it != verified_.end();) {
        if (it->second.sequence <= sequence) {
            out.emplace_back(it->first, it->second);
            it = verified_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

DigContext dig_context(const mcpp::Session& session, const mcpp::world::Body& body, const ToolOnBlock& tool) {
    DigContext dig;
    const int32_t me = session.entity_id();
    dig.tool_speed = tool.speed;
    dig.correct_tool = tool.correct;
    dig.mining_efficiency = session.attribute(me, "minecraft:mining_efficiency", 0.0);
    dig.block_break_speed = session.attribute(me, "minecraft:block_break_speed", 1.0);
    dig.submerged_mining_speed = session.attribute(me, "minecraft:submerged_mining_speed", 0.2);
    dig.haste = session.effect_level("minecraft:haste");
    dig.conduit_power = session.effect_level("minecraft:conduit_power");
    dig.mining_fatigue = session.effect_level("minecraft:mining_fatigue");
    dig.eye_in_water = body.eye_in_water;
    dig.on_ground = body.on_ground;
    return dig;
}

// `Player.blockActionRestricted`: spectators never, adventure only with an item allowed to.
bool MultiPlayerGameMode::restricted() const {
    const int32_t mode = session_.vitals().game_mode;
    if (mode != 2 && mode != 3) return false;
    if (mode == 3) return true;
    const auto& held = level_.held();
    if (held.count <= 0) return true;
    return held.components.find("minecraft:can_break", mcpp::protocol::kTargetProtocol) == nullptr;
}

// `MultiPlayerGameMode.sameDestroyTarget`: the same block with the same item in hand.
bool MultiPlayerGameMode::same_destroy_target(const int32_t pos[3]) const {
    return std::equal(pos, pos + 3, destroy_pos_) && same_item_same_components(level_.held(), destroying_item_);
}

// `MultiPlayerGameMode.startPrediction`: a change shown now, sent under the next sequence.
template <typename Predict> void MultiPlayerGameMode::start_prediction(Predict&& predict) {
    const int32_t sequence = session_.sequence() + 1;
    prediction_.start(sequence);
    predict();
    prediction_.stop();
}

// `MultiPlayerGameMode.destroyBlock`, `Block.playerWillDestroy`: the block gone on our side.
bool MultiPlayerGameMode::destroy_block(const int32_t pos[3]) {
    if (restricted()) return false;
    const int32_t state = level_.state_at(pos);
    const auto& shapes = BlockShapes::game();
    const auto tool = tool_on_block(ToolTable::game().find(level_.held().count > 0 ? level_.held().id : -1), state,
                                    shapes, session_.tags());
    // `Item.canDestroyBlock`: a sword breaks nothing in creative.
    if (!tool.creative_breaks && session_.abilities().instant_build) return false;
    const auto& name = shapes.name(state);
    if ((name == "minecraft:command_block" || name == "minecraft:chain_command_block" ||
         name == "minecraft:repeating_command_block" || name == "minecraft:structure_block" ||
         name == "minecraft:jigsaw" || name == "minecraft:test_block" ||
         name == "minecraft:test_instance_block") &&
        !(session_.abilities().instant_build && permission_level >= 2)) {
        return false;
    }
    if ((shapes.state(state).flags & mcpp::world::kAir) != 0) return false;
    level_.break_effect(pos, state);
    // `FluidState.createLegacyBlock`: what stays behind is the block's fluid, or air.
    int32_t left = 0;
    const auto& info = shapes.state(state);
    if (info.fluid == mcpp::world::Fluid::Water && info.fluid_source) {
        left = shapes.find("minecraft:water");
    }
    const int32_t before = state;
    const bool changed = level_.set_block(pos, std::max(0, left));
    if (changed && prediction_.predicting()) {
        double player[3];
        level_.player_position(player);
        prediction_.retain(pos, before, player);
    }
    return changed;
}

// `MultiPlayerGameMode.startDestroyBlock`: a dig begun, or a creative break.
bool MultiPlayerGameMode::start_destroy_block(const int32_t pos[3], int32_t face) {
    if (restricted()) return false;
    if (session_.abilities().instant_build) {
        start_prediction([&] {
            destroy_block(pos);
            session_.send_dig_action(0, pos[0], pos[1], pos[2], face);
        });
        destroy_delay_ = 5;
    } else if (!destroying_ || !same_destroy_target(pos)) {
        if (destroying_) {
            session_.send_dig_action(1, destroy_pos_[0], destroy_pos_[1], destroy_pos_[2], face);
        }
        const int32_t state = level_.state_at(pos);
        start_prediction([&] {
            const bool solid = (BlockShapes::game().state(state).flags & mcpp::world::kAir) == 0;
            if (solid && level_.destroy_progress(state) >= 1.0f) {
                destroy_block(pos);
            } else {
                destroying_ = true;
                std::copy(pos, pos + 3, destroy_pos_);
                destroying_item_ = level_.held();
                destroy_progress_ = 0.0f;
                destroy_ticks_ = 0.0f;
                level_.progress(destroy_pos_, destroy_stage());
            }
            session_.send_dig_action(0, pos[0], pos[1], pos[2], face);
        });
    }
    return true;
}

// `MultiPlayerGameMode.stopDestroyBlock`: a dig given up.
void MultiPlayerGameMode::stop_destroy_block() {
    if (!destroying_) return;
    session_.send_dig_action(1, destroy_pos_[0], destroy_pos_[1], destroy_pos_[2], 0);
    destroying_ = false;
    destroy_progress_ = 0.0f;
    level_.progress(destroy_pos_, -1);
}

// `MultiPlayerGameMode.continueDestroyBlock`: one more tick of digging.
bool MultiPlayerGameMode::continue_destroy_block(const int32_t pos[3], int32_t face) {
    if (destroy_delay_ > 0) {
        --destroy_delay_;
        return true;
    }
    if (session_.abilities().instant_build) {
        destroy_delay_ = 5;
        start_prediction([&] {
            destroy_block(pos);
            session_.send_dig_action(0, pos[0], pos[1], pos[2], face);
        });
        return true;
    }
    if (!same_destroy_target(pos)) return start_destroy_block(pos, face);
    const int32_t state = level_.state_at(pos);
    if ((BlockShapes::game().state(state).flags & mcpp::world::kAir) != 0) {
        destroying_ = false;
        return false;
    }
    destroy_progress_ = destroy_progress_ + level_.destroy_progress(state);
    if (std::fmod(destroy_ticks_, 4.0f) == 0.0f) level_.hit_sound(pos, state);
    ++destroy_ticks_;
    if (destroy_progress_ >= 1.0f) {
        destroying_ = false;
        start_prediction([&] {
            destroy_block(pos);
            session_.send_dig_action(2, pos[0], pos[1], pos[2], face);
        });
        destroy_progress_ = 0.0f;
        destroy_ticks_ = 0.0f;
        destroy_delay_ = 5;
    }
    level_.progress(destroy_pos_, destroy_stage());
    return true;
}

// `MultiPlayerGameMode.getDestroyStage`: the crack, 0 to 9, or none.
int32_t MultiPlayerGameMode::destroy_stage() const {
    return destroy_progress_ > 0.0f ? mcpp::jmath::to_int(destroy_progress_ * 10.0f) : -1;
}

} // namespace client
