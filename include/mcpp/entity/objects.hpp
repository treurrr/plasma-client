#pragma once
// The entities that are not bodies: what a lobby is actually full of. A
// dropped item, a hologram, an armour stand's props, a boat, a minecart, a
// projectile, an orb. `src/entity/mobs.*` draws anything with a
// `LivingEntityRenderer`; everything here has an `EntityRenderer` of its own,
// which means its own placement and, for half of them, geometry that is not a
// model layer at all.
//
// Every renderer here is translated as a pure function: given one entity's
// render state it appends the pieces the game submits, in the order the game
// submits them, so a probe can ask for them without a window.
#include "mcpp/entity/model.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/gpu/hand.hpp"
#include "mcpp/session.hpp"
#include "mcpp/ui/items.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

// `EntityRenderers.createEntityRenderers`: the renderer one of these types
// has, where the renderers that differ only in a texture or a model share one.
enum class ObjectKind : uint8_t {
    None,
    // `ItemEntityRenderer`.
    Item,
    // `ArrowRenderer`.
    Arrow,
    // `ThrownTridentRenderer`.
    Trident,
    // `ThrownItemRenderer`.
    ThrownItem,
    // `WindChargeRenderer`.
    WindCharge,
    // `AbstractBoatRenderer`, `BoatRenderer` and `RaftRenderer`.
    Boat,
    // `AbstractMinecartRenderer`.
    Minecart,
    // `LeashKnotRenderer`.
    LeashKnot,
    // `ExperienceOrbRenderer`.
    ExperienceOrb,
    // `FishingHookRenderer`.
    FishingBobber,
    // `TntRenderer`.
    Tnt,
    // `FallingBlockRenderer`.
    FallingBlock,
    // `DisplayRenderer.BlockDisplayRenderer`.
    BlockDisplay,
    // `DisplayRenderer.ItemDisplayRenderer`.
    ItemDisplay,
    // `DisplayRenderer.TextDisplayRenderer`.
    TextDisplay,
};

// One entity type as its own renderer draws it.
struct ObjectType {
    std::string_view type;          // "minecraft:oak_boat"
    ObjectKind kind = ObjectKind::None;
    std::string_view layer;         // the baked model layer, empty for none
    std::string_view texture;       // under textures/entity/, no extension
    float shadow_radius = 0.0f;
    // `ThrownItemRenderer`: the scale the item is drawn at, and whether the
    // renderer overrides the block light to fifteen.
    float scale = 1.0f;
    bool full_bright = false;
    // `ItemSupplier.getItem`: the item a projectile is drawn as when the
    // server sends an empty stack, as `*Item.getDefaultItem` names it.
    std::string_view default_item;
    gpu::EntityRenderer::Type render = gpu::EntityRenderer::Type::Cutout;
};

// Every object type this client draws, by registry name; nullptr for one it
// does not, which includes every living body (those are `mob_type`).
const ObjectType* object_type(std::string_view type);
std::span<const ObjectType> object_types();

// `Display.BillboardConstraints`.
enum class Billboard : uint8_t { Fixed, Vertical, Horizontal, Center };

// `Display.TextDisplay.Align`.
enum class TextAlign : uint8_t { Center, Left, Right };

// Everything the renderers here read, flattened the way `MobState` flattens
// the living ones: the easing the client did, and the `set_entity_data` fields
// of every type above.
struct ObjectState {
    int32_t entity_id = 0;
    // Where the entity is, in blocks, relative to the camera, already eased.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    // Degrees, eased.
    float y_rot = 0.0f, x_rot = 0.0f;
    float age_in_ticks = 0.0f;
    float partial = 0.0f;
    uint8_t sky_light = 15, block_light = 0;
    bool invisible = false;
    // Where the camera looks, for the renderers that billboard.
    float camera_y_rot = 0.0f, camera_x_rot = 0.0f;

    // `ItemClusterRenderState`: the stack, and the bob it was spawned with.
    ItemStack stack;
    float bob_offset = 0.0f;

    // `BoatRenderState`, `MinecartRenderState`: the knock it took and the
    // bubble column it is in.
    float hurt_time = 0.0f;
    int32_t hurt_dir = 1;
    float damage_time = 0.0f;
    float bubble_angle = 0.0f;
    bool under_water = false;
    float rowing_left = 0.0f, rowing_right = 0.0f;
    // `MinecartRenderState`: the block it carries, and how high it sits.
    int32_t display_block = 0;
    int32_t display_offset = 6;
    bool has_display_block = false;

    // `TntRenderState`: ticks of fuse left, and the block it is made of.
    float fuse_remaining = 80.0f;
    int32_t block_state = 0;

    // `ExperienceOrbRenderState.icon`.
    int32_t orb_icon = 0;

    // `FishingHookRenderState.lineOriginOffset`: the rod's tip relative to
    // the hook, in blocks.
    std::array<float, 3> line_origin{};

    // `DisplayEntityRenderState`: the transformation the server sends, how far
    // into its interpolation it is, and what it is constrained to.
    std::array<float, 3> translation{};
    std::array<float, 3> display_scale{1.0f, 1.0f, 1.0f};
    std::array<float, 4> rotation_left{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> rotation_right{0.0f, 0.0f, 0.0f, 1.0f};
    Billboard billboard = Billboard::Fixed;
    int32_t brightness_override = -1;
    // `Display.TextDisplay`: the lines, already split to `line_width`.
    std::vector<std::string> lines;
    uint32_t background_colour = 0x40000000u;
    uint8_t text_opacity = 255;
    TextAlign align = TextAlign::Center;
    bool text_see_through = false;
    bool text_shadow = false;
    bool default_background = false;
    // `Display.ItemDisplay`: the context its own transform comes from, as
    // `ItemDisplayContext`'s own id.
    uint8_t item_display_context = 0;
};

// One piece of one object entity, already placed: what `draw` hands the
// renderers and what a probe compares with the game.
struct ObjectPiece {
    enum class What : uint8_t { Model, Item, Text, Quads };
    What what = What::Model;

    // Where this piece's own space sits, relative to the camera.
    Matrix root = identity();
    uint8_t sky_light = 15, block_light = 0;

    // Model: a baked layer, the picture over it and the pose it is in.
    const Model* model = nullptr;
    std::string_view texture;
    Pose pose;
    gpu::EntityRenderer::Type render = gpu::EntityRenderer::Type::Cutout;
    uint32_t colour = 0xFFFFFFFFu;

    // Item: the item's own id. `root` takes the item's own space - a block
    // from corner to corner, its display transform already applied - to
    // camera-relative space, which is what `HandRenderer::add_world_item`
    // wants.
    int32_t item = -1;

    // Text: one line, with the colour and the background behind it.
    std::string text;
    uint32_t text_colour = 0xFFFFFFFFu;
    uint32_t background = 0;
    bool see_through = false;

    // Quads: four corners at a time, in this piece's own space, against
    // `texture`.
    struct Corner {
        float position[3]{};
        float uv[2]{};
        uint32_t colour = 0xFFFFFFFFu;
    };
    std::vector<Corner> corners;
};

// What a renderer needs besides the state: where a model layer and an item
// come from. `HandRenderer` owns the item geometry and its display
// transforms; `ItemTable` turns a block's own name into the item that carries
// its model.
struct ObjectAssets {
    // A baked model layer by name; nullptr for one that is not loaded.
    std::function<const Model*(std::string_view)> layer;
    const gpu::HandRenderer* hand = nullptr;
    const ui::ItemTable* items = nullptr;
    // Block state -> the block's registry name, as the baked states table
    // gives it; empty when there is none.
    std::function<std::string_view(int32_t)> block_name;
};

// `EntityRenderer.submit` for one object entity: appends every piece, in the
// order the game submits them. `root` is where the entity stands, relative to
// the camera, with nothing else in it.
void build_object(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                  std::vector<ObjectPiece>& out);

// `ItemClusterRenderState.getRenderedAmount`: how many copies of a stack are
// drawn.
int rendered_amount(int count);
// `ItemClusterRenderState.getSeedForItemStack`.
int32_t item_cluster_seed(const ItemStack& stack);
// `ItemEntity.getSpin`.
float item_spin(float age, float bob_offset);

// `ItemTransform` of the `GROUND` context, which the held-item bake does not
// carry: vanilla's whole model tree defines it in fifteen places, and every
// item inherits one of them. `flat` is whether the item's own model is a
// sprite extruded a sixteenth of a block, which is what tells the
// `item/generated` family from the `block/block` one.
gpu::HandRenderer::Display ground_display(std::string_view item_name, bool flat);

} // namespace mcpp::entity
