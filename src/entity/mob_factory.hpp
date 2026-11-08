#pragma once
#include "mcpp/entity/mobs.hpp"

// Each family of mobs lives in its own translation unit and offers the same
// two things: the model for one of its layers, and the types it draws.
namespace mcpp::entity {

std::unique_ptr<MobModel> make_humanoid_mob_model(std::string_view layer, const Model& model);
std::span<const MobType> humanoid_mob_types();

std::unique_ptr<MobModel> make_animal_model(std::string_view layer, const Model& model);
std::span<const MobType> animal_mob_types();

std::unique_ptr<MobModel> make_monster_model(std::string_view layer, const Model& model);
std::span<const MobType> monster_mob_types();

std::unique_ptr<MobModel> make_keyframe_model(std::string_view layer, const Model& model);
std::span<const MobType> keyframe_mob_types();

std::unique_ptr<MobModel> make_armor_stand_model(std::string_view layer, const Model& model);
std::span<const MobType> armor_stand_mob_types();

} // namespace mcpp::entity
