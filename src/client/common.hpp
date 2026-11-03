#pragma once
// What the subcommands in client_main.cpp and the interactive client share.
#include "mcpp/auth.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace client {

// A signed-in account if there is one. The client does not refuse to run
// without it, but a server in online mode will.
mcpp::auth::Profile signed_in_profile(std::string& username);

// What the game calls an item, from its own language file: "Totem of Undying",
// "Bottle o' Enchanting", "TNT". Casing the registry name gets a third of them
// wrong, so that is only the fallback for an install with no language file.
std::string item_display_name(std::string_view name);

// The block the crosshair is on, or none within reach. Reach is the
// block_interaction_range attribute, 4.5 in survival and 5 in creative, and
// the game takes the first block the ray enters, so this steps cell by cell
// rather than sampling along the ray, where a corner can be missed.
bool aimed_block(const float eye[3], const float direction[3], float reach,
                 const mcpp::world::BlockAt& at, const mcpp::world::BlockTable& table,
                 int32_t out[3]);

struct Address {
    std::string host;
    uint16_t port = 25565;
};

Address parse_address(const std::string& target);

float ground_near_centre(const std::vector<mcpp::world::Chunk>& chunks,
                         const mcpp::world::BlockTable& table, float centre_x, float centre_z);

// Unpacks every chunk once - a neighbour is read by four others - then meshes
// every section of every chunk.
std::vector<mcpp::world::SectionMesh> mesh_world(const std::vector<mcpp::world::Chunk>& chunks,
                                                 const mcpp::world::BlockTable& table,
                                                 double* milliseconds);

} // namespace client
