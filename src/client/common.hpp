#pragma once
// What the subcommands in client_main.cpp and the interactive client share.
#include "mcpp/auth.hpp"
#include "mcpp/world/block_table.hpp"
#include "mcpp/world/chunk.hpp"
#include "mcpp/world/mesher.hpp"
#include "mcpp/world/physics.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {
class Session;
}

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
// The four tints per biome the server listed, with the colour maps resolved
// and the blend radius PLASMA_BIOME_BLEND asks for.
mcpp::world::BiomeTints biome_tints_of(const mcpp::Session& session);

std::vector<mcpp::world::SectionMesh> mesh_world(std::span<const mcpp::world::Chunk> chunks,
                                                 const mcpp::world::BlockTable& table,
                                                 double* milliseconds,
                                                 double* unpack_milliseconds = nullptr,
                                                 const mcpp::world::BiomeTints& tints = {});

// Room for the world's geometry, in bytes of vertices and bytes of indices.
struct GeometryBudget {
    size_t vertex_bytes = 0;
    size_t index_bytes = 0;
    // What the sizing was worked out from, so a report can say why it is this
    // size and not another.
    size_t chunks_wanted = 0;
    uint64_t device_free = 0;
    bool whole_distance = false;
};

// How much to reserve so a render distance fits. Measured on the four
// benchmark worlds, a chunk is 0.37 to 0.58 MB of vertices and indices
// together, so (2d+1)^2 chunks at the widest of those is what a render
// distance costs; the device's own free memory, less what the rest of the
// frame needs, is the ceiling.
GeometryBudget geometry_budget(uint64_t device_local_free, int32_t render_distance);

} // namespace client
