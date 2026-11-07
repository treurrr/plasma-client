#include "mcpp/entity/block_entities.hpp"

#include "mcpp/jmath.hpp"
#include "mcpp/world/chunk.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <format>
#include <sstream>
#include <stdexcept>

namespace mcpp::entity {
namespace {

constexpr float kRadians = 3.1415927f / 180.0f;
constexpr float kPi = 3.1415927f;

// `Direction.toYRot`: a horizontal face's turn, south first.
constexpr float kYRot[6] = {0.0f, 0.0f, 180.0f, 0.0f, 90.0f, 270.0f};
// `Direction.getStepX`, `Direction.getStepZ`.
constexpr float kStepX[6] = {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f};
constexpr float kStepZ[6] = {0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f};
// `Direction.getOpposite`.
constexpr int kOpposite[6] = {1, 0, 3, 2, 5, 4};
// `Direction.get2DDataValue`, which a rotation segment is counted from.
constexpr int k2dData[6] = {0, 0, 2, 0, 1, 3};
// `Direction.getClockWise`.
constexpr int kClockWise[6] = {0, 0, 5, 4, 2, 3};

// `DyeColor.getName`, in id order: what a banner layer or a sign's ink names.
constexpr const char* kColourNames[16] = {
    "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
    "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

// `DyeColor`: the colour a sign's text is written in.
constexpr uint32_t kTextColours[16] = {
    0xFFFFFF, 0xFF681F, 0xFF00FF, 0x9AC0CD, 0xFFFF00, 0xC0FF00, 0xFF9AC0, 0x808080,
    0xD3ECEC, 0x00FFFF, 0xA02FF0, 0x0000FF, 0x8B5A2B, 0x00FF00, 0xFF0000, 0x000000};
// `DyeColor`: what a dyed pattern layer is multiplied by.
constexpr uint32_t kDyeColours[16] = {
    0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F, 0xF38BAA, 0x474F52,
    0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA, 0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};

// `SegmentedAnglePrecision.toDegrees` with sixteen segments: half a turn comes
// out the short way round.
float segment_degrees(int segment) {
    const float degrees = static_cast<float>(segment & 15) * 22.5f;
    return degrees >= 180.0f ? degrees - 360.0f : degrees;
}

// `RotationSegment.convertToSegment`: which sixteenth a face points along.
int direction_segment(int facing) {
    return facing < 2 ? 0 : k2dData[facing] * 4;
}

// `Direction.getRotation`: how a face's own space is turned into the world's.
std::array<float, 4> direction_rotation(int facing) {
    switch (facing) {
    case 0: return quaternion_x(kPi);
    case 1: return {0.0f, 0.0f, 0.0f, 1.0f};
    case 2: return quaternion_xyz(kPi / 2.0f, 0.0f, kPi);
    case 3: return quaternion_x(kPi / 2.0f);
    case 4: return quaternion_xyz(kPi / 2.0f, 0.0f, kPi / 2.0f);
    default: return quaternion_xyz(kPi / 2.0f, 0.0f, -kPi / 2.0f);
    }
}

// `ARGB.opaque`: a colour with nothing see-through about it.
uint32_t opaque(uint32_t rgb) { return 0xFF000000u | rgb; }

// `ARGB.scaleRGB`: a sign's text, dimmed, is what is drawn without a glow.
uint32_t scale_rgb(uint32_t argb, float factor) {
    const auto channel = [&](int shift) {
        const auto value = static_cast<float>((argb >> shift) & 0xFFu) * factor;
        return static_cast<uint32_t>(std::clamp(value, 0.0f, 255.0f)) << shift;
    };
    return (argb & 0xFF000000u) | channel(16) | channel(8) | channel(0);
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

int32_t to_int(std::string_view text, int32_t fallback = 0) {
    int32_t value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

BlockEntityKind kind_of(std::string_view name) {
    if (name == "chest") return BlockEntityKind::Chest;
    if (name == "sign") return BlockEntityKind::Sign;
    if (name == "hanging_sign") return BlockEntityKind::HangingSign;
    if (name == "bed") return BlockEntityKind::Bed;
    if (name == "banner") return BlockEntityKind::Banner;
    if (name == "skull") return BlockEntityKind::Skull;
    if (name == "shulker_box") return BlockEntityKind::ShulkerBox;
    if (name == "bell") return BlockEntityKind::Bell;
    if (name == "conduit") return BlockEntityKind::Conduit;
    if (name == "decorated_pot") return BlockEntityKind::DecoratedPot;
    if (name == "enchanting_table") return BlockEntityKind::EnchantingTable;
    if (name == "lectern") return BlockEntityKind::Lectern;
    if (name == "copper_golem_statue") return BlockEntityKind::CopperGolemStatue;
    return BlockEntityKind::None;
}

// `BookModel.setupAnim`: how far the two covers and the four pages are open.
void book_pose(Pose& pose, const Model& model, float animation, float flip1, float flip2,
               float open);

void set_rotation(Pose& pose, const Model& model, std::string_view path, float x, float y,
                  float z) {
    const int32_t part = model.find(path);
    if (part < 0) return;
    pose[part].x_rot = x;
    pose[part].y_rot = y;
    pose[part].z_rot = z;
}

void book_pose(Pose& pose, const Model& model, float animation, float flip1, float flip2,
               float open) {
    const float spread = (jmath::sin(animation * 0.02f) * 0.1f + 1.25f) * open;
    set_rotation(pose, model, "left_lid", 0.0f, kPi + spread, 0.0f);
    set_rotation(pose, model, "right_lid", 0.0f, -spread, 0.0f);
    set_rotation(pose, model, "left_pages", 0.0f, spread, 0.0f);
    set_rotation(pose, model, "right_pages", 0.0f, -spread, 0.0f);
    set_rotation(pose, model, "flip_page1", 0.0f, spread - spread * 2.0f * flip1, 0.0f);
    set_rotation(pose, model, "flip_page2", 0.0f, spread - spread * 2.0f * flip2, 0.0f);
    for (const char* part : {"left_pages", "right_pages", "flip_page1", "flip_page2"}) {
        const int32_t index = model.find(part);
        if (index >= 0) pose[index].x = jmath::sin(spread);
    }
}

} // namespace

bool BlockEntityTable::open(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    std::stringstream text;
    text << input.rdbuf();
    parse(text.str());
    return true;
}

int16_t BlockEntityTable::intern(std::vector<std::string>& pool,
                                 std::map<std::string, int16_t, std::less<>>& by,
                                 std::string_view name) {
    if (const auto found = by.find(name); found != by.end()) return found->second;
    const auto index = static_cast<int16_t>(pool.size());
    pool.emplace_back(name);
    by.emplace(std::string(name), index);
    return index;
}

void BlockEntityTable::parse(std::string_view text) {
    size_t number = 0;
    while (!text.empty()) {
        ++number;
        const size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#') continue;

        const size_t space = line.find(' ');
        if (space == std::string_view::npos) {
            throw std::runtime_error(std::format("block entities, line {}: no kind", number));
        }
        const int32_t state = to_int(line.substr(0, space), -1);
        line.remove_prefix(space + 1);
        const size_t next = line.find(' ');
        BlockEntityRow row;
        row.kind = kind_of(line.substr(0, std::min(next, line.size())));
        if (row.kind == BlockEntityKind::None || state < 0) {
            throw std::runtime_error(std::format("block entities, line {}: unknown row", number));
        }
        line = next == std::string_view::npos ? std::string_view{} : line.substr(next + 1);

        while (!line.empty()) {
            const size_t gap = line.find(' ');
            std::string_view field = line.substr(0, gap);
            line = gap == std::string_view::npos ? std::string_view{} : line.substr(gap + 1);
            const size_t equals = field.find('=');
            if (equals == std::string_view::npos) continue;
            const std::string_view key = field.substr(0, equals);
            const std::string_view value = field.substr(equals + 1);
            if (key == "model") row.model = intern(layers_, layer_index_, value);
            else if (key == "tex") row.texture = intern(textures_, texture_index_, value);
            else if (key == "facing") row.facing = static_cast<int8_t>(to_int(value, -1));
            else if (key == "rot") row.rotation = static_cast<int8_t>(to_int(value, -1));
            else if (key == "colour") row.colour = static_cast<int8_t>(to_int(value, -1));
            else if (key == "variant") row.variant = static_cast<uint8_t>(to_int(value));
            else if (key == "flag") row.flag = static_cast<uint8_t>(to_int(value));
            else throw std::runtime_error(std::format("block entities, line {}: '{}'?", number, key));
        }
        rows_.emplace(state, row);
    }
}

const BlockEntityRow* BlockEntityTable::find(int32_t state) const {
    const auto found = rows_.find(state);
    return found == rows_.end() ? nullptr : &found->second;
}

std::string_view BlockEntityTable::layer(int16_t index) const {
    return index < 0 ? std::string_view{} : std::string_view(layers_[static_cast<size_t>(index)]);
}

std::string_view BlockEntityTable::texture(int16_t index) const {
    return index < 0 ? std::string_view{} : std::string_view(textures_[static_cast<size_t>(index)]);
}

std::filesystem::path BlockEntityTable::default_path(int32_t protocol) {
    const char* home = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base =
        home != nullptr ? std::filesystem::path(home)
                        : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".") /
                              ".local" / "share";
    return base / "plasma-client" / "data" / std::format("block_entities-{}.txt", protocol);
}

BlockEntityScene::Key BlockEntityScene::key_of(int32_t x, int32_t y, int32_t z) {
    return {(static_cast<int64_t>(x) & 0x3FFFFFF) << 38 | (static_cast<int64_t>(z) & 0x3FFFFFF) << 12
            | (static_cast<int64_t>(y) & 0xFFF)};
}

void BlockEntityScene::load(const Assets& assets, const BlockEntityTable& table,
                            skins::Cache* skins) {
    table_ = &table;
    skins_ = skins;
    assets_ = &assets;
    models_ = &assets.models();
    font_ = &assets.font();
}

// `SkullBlockRenderer.resolveSkullRenderType`: a player head wears the skin
// its profile names, which is a download the skin cache has already made for
// every player in the tab list.
int32_t BlockEntityScene::skin_for(gpu::EntityRenderer& renderer, const nbt::Value& data) {
    if (skins_ == nullptr) return -1;
    const nbt::Value* profile = data.find("profile");
    if (profile == nullptr) return -1;
    const nbt::Value* properties = profile->find("properties");
    if (properties == nullptr) return -1;
    const nbt::List* list = properties->as_list();
    if (list == nullptr) return -1;
    for (const nbt::Value& property : *list) {
        const nbt::Value* name = property.find("name");
        const nbt::Value* value = property.find("value");
        if (name == nullptr || value == nullptr) continue;
        const std::string* key = name->as_string();
        const std::string* encoded = value->as_string();
        if (key == nullptr || encoded == nullptr || *key != "textures") continue;
        const auto textures = skins::decode_property(*encoded);
        if (!textures || textures->skin_url.empty()) return -1;
        const auto found = skin_layers_.find(textures->skin_url);
        if (found != skin_layers_.end()) return found->second.first;
        const entity::Texture* skin = skins_->get(textures->skin_url);
        if (skin == nullptr) return -1;
        entity::Texture copy = *skin;
        if (!skins::normalise(copy)) return -1;
        const int32_t layer = renderer.upload_skin(copy);
        if (layer < 0) return -1;
        skin_layers_.emplace(textures->skin_url, std::pair<int32_t, bool>{layer, textures->slim});
        return layer;
    }
    return -1;
}

void BlockEntityScene::put(int32_t x, int32_t y, int32_t z, nbt::Value data) {
    BlockEntity& entity = entities_[key_of(x, y, z)];
    entity.x = x;
    entity.y = y;
    entity.z = z;
    entity.data = std::move(data);
}

void BlockEntityScene::appear(int32_t x, int32_t y, int32_t z) {
    const auto [at, fresh] = entities_.try_emplace(key_of(x, y, z));
    if (!fresh) return;
    at->second.x = x;
    at->second.y = y;
    at->second.z = z;
}

void BlockEntityScene::removed(int32_t x, int32_t y, int32_t z) {
    entities_.erase(key_of(x, y, z));
}

void BlockEntityScene::forget_chunk(int32_t chunk_x, int32_t chunk_z) {
    for (auto it = entities_.begin(); it != entities_.end();) {
        const BlockEntity& entity = it->second;
        if ((entity.x >> 4) == chunk_x && (entity.z >> 4) == chunk_z) it = entities_.erase(it);
        else ++it;
    }
}

// `ChestBlockEntity.triggerEvent`: the one event a chest or a shulker box has
// is how many players are looking inside.
void BlockEntityScene::event(int32_t x, int32_t y, int32_t z, uint8_t action,
                             uint8_t parameter) {
    if (action != 1) return;
    const auto found = entities_.find(key_of(x, y, z));
    if (found == entities_.end()) return;
    found->second.viewers = parameter;
}

void BlockEntityScene::clear() { entities_.clear(); }

// `ChestLidController.tickLid`: a tenth of the way shut, or a tenth of the way
// open, every tick.
void BlockEntityScene::tick() {
    for (auto& [key, entity] : entities_) {
        entity.last_openness = entity.openness;
        ++entity.ticks;
        const float wanted = entity.viewers > 0 ? 1.0f : 0.0f;
        if (entity.openness < wanted) entity.openness = std::min(wanted, entity.openness + 0.1f);
        else if (entity.openness > wanted) entity.openness = std::max(wanted, entity.openness - 0.1f);
    }
}

namespace {

// `ChestRenderer.submit`: the block's corner, turned to the chest's facing.
Placement chest_pose(const Placement& root, int facing) {
    Placement m = root;
    m.translate(0.5f, 0.5f, 0.5f);
    m.rotate(quaternion_y(to_radians(-kYRot[facing])));
    m.translate(-0.5f, -0.5f, -0.5f);
    return m;
}

// `BedRenderer.preparePose`: the piece laid flat and turned to its facing.
Placement bed_pose(const Placement& root, int facing) {
    Placement m = root;
    m.translate(0.0f, 0.5625f, 0.0f);
    m.rotate(quaternion_x(to_radians(90.0f)));
    m.translate(0.5f, 0.5f, 0.5f);
    m.rotate(quaternion_z(to_radians(180.0f + kYRot[facing])));
    m.translate(-0.5f, -0.5f, -0.5f);
    return m;
}

// `ShulkerBoxRenderer.prepareModel`: stood on the face it hangs from.
Placement shulker_pose(const Placement& root, int facing) {
    Placement m = root;
    m.translate(0.5f, 0.5f, 0.5f);
    m.scale(0.9995f, 0.9995f, 0.9995f);
    m.rotate(direction_rotation(facing));
    m.scale(1.0f, -1.0f, -1.0f);
    m.translate(0.0f, -1.0f, 0.0f);
    return m;
}

// `SignRenderer.translateSign`, `HangingSignRenderer.translateBase`.
Placement sign_pose(const Placement& root, const BlockEntityRow& row, bool hanging) {
    const float yaw = row.rotation >= 0 ? segment_degrees(row.rotation) : kYRot[row.facing];
    Placement m = root;
    if (hanging) {
        m.translate(0.5f, 0.9375f, 0.5f);
        m.rotate(quaternion_y(to_radians(-yaw)));
        m.translate(0.0f, -0.3125f, 0.0f);
    } else {
        m.translate(0.5f, 0.5f, 0.5f);
        m.rotate(quaternion_y(to_radians(-yaw)));
        if (row.variant == 1) m.translate(0.0f, -0.3125f, -0.4375f);
    }
    return m;
}

// `BannerRenderer.submitBanner`: two thirds the size, hanging the other way up.
Placement banner_pose(const Placement& root, const BlockEntityRow& row) {
    const float angle = row.rotation >= 0 ? -segment_degrees(row.rotation) : -kYRot[row.facing];
    Placement m = root;
    m.translate(0.5f, 0.0f, 0.5f);
    m.rotate(quaternion_y(to_radians(angle)));
    m.scale(0.6666667f, -0.6666667f, -0.6666667f);
    return m;
}

// `SkullBlockRenderer.submitSkull`: on the floor, or a quarter out of the wall.
Placement skull_pose(const Placement& root, const BlockEntityRow& row) {
    Placement m = root;
    if (row.variant == 1) {
        m.translate(0.5f - kStepX[row.facing] * 0.25f, 0.25f,
                    0.5f - kStepZ[row.facing] * 0.25f);
    } else {
        m.translate(0.5f, 0.0f, 0.5f);
    }
    m.scale(-1.0f, -1.0f, 1.0f);
    return m;
}

const nbt::Value* side_text(const BlockEntity& entity, bool front) {
    return entity.data.find(front ? "front_text" : "back_text");
}

} // namespace

void BlockEntityScene::build(const BlockEntityRow& row, const BlockEntityTable& table,
                             const BlockEntity& entity, const ModelSet& models,
                             const Placement& root, float partial, int64_t game_time,
                             std::vector<Piece>& out) {
    const Model* model = models.find(table.layer(row.model));
    if (model == nullptr) return;
    const std::string_view texture = table.texture(row.texture);
    Piece piece;
    piece.model = model;
    piece.texture = texture;
    piece.pose.reset(*model);

    switch (row.kind) {
    case BlockEntityKind::Chest: {
        // `ChestRenderer.submit`: the lid eases the last of the way open.
        float open = entity.last_openness + (entity.openness - entity.last_openness) * partial;
        open = 1.0f - open;
        open = 1.0f - open * open * open;
        piece.root = chest_pose(root, row.facing).matrix;
        // `ChestModel.setupAnim`: the lid and its lock turn back together.
        const float lid = -(open * (kPi / 2.0f));
        set_rotation(piece.pose, *model, "lid", lid, 0.0f, 0.0f);
        set_rotation(piece.pose, *model, "lock", lid, 0.0f, 0.0f);
        piece.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::Sign:
    case BlockEntityKind::HangingSign: {
        const bool hanging = row.kind == BlockEntityKind::HangingSign;
        // `AbstractSignRenderer.submitSign`: the model alone is shrunk.
        const float scale = hanging ? 1.0f : 0.6666667f;
        Placement place = sign_pose(root, row, hanging);
        place.scale(scale, -scale, -scale);
        piece.root = place.matrix;
        piece.type = gpu::EntityRenderer::Type::CutoutNoCull;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::Bed:
        piece.root = bed_pose(root, row.facing).matrix;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(std::move(piece));
        break;
    case BlockEntityKind::Banner: {
        const Matrix place = banner_pose(root, row).matrix;
        piece.root = place;
        piece.texture = texture;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(piece);

        const Model* flag = models.find(std::string(table.layer(row.model)) + "#flag");
        if (flag == nullptr) break;
        // `BannerFlagModel.setupAnim`: the cloth sways on a hundred tick loop
        // offset by where the banner stands.
        const int64_t beat = entity.x * 7 + entity.y * 9 + entity.z * 13 + game_time;
        const float phase =
            (static_cast<float>(((beat % 100) + 100) % 100) + partial) / 100.0f;
        Pose cloth(*flag);
        set_rotation(cloth, *flag, "flag",
                     (-0.0125f + 0.01f * jmath::cos(2.0f * kPi * phase)) * kPi, 0.0f, 0.0f);

        Piece banner;
        banner.model = flag;
        banner.root = place;
        banner.pose = cloth;
        banner.texture = texture;
        banner.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(banner);
        // `BannerRenderer.submitPatternLayer`: the base colour over the cloth,
        // then a layer per pattern. The patterns themselves are the stack's.
        banner.texture = "banner/base";
        banner.colour = opaque(kDyeColours[row.colour < 0 ? 0 : row.colour & 15]);
        banner.type = gpu::EntityRenderer::Type::CutoutNoCull;
        out.push_back(banner);
        if (const nbt::Value* patterns = entity.data.find("patterns")) {
            if (const nbt::List* list = patterns->as_list()) {
                for (size_t i = 0; i < list->size() && i < 16; ++i) {
                    const nbt::Value* asset = (*list)[i].find("pattern");
                    const nbt::Value* dye = (*list)[i].find("color");
                    if (asset == nullptr) continue;
                    const std::string* name = asset->as_string();
                    if (name == nullptr) continue;
                    Piece layer = banner;
                    const size_t colon = name->find(':');
                    layer.texture = "banner/";
                    layer.texture.append(std::string_view(*name).substr(
                        colon == std::string::npos ? 0 : colon + 1));
                    layer.colour = opaque(kDyeColours[0]);
                    if (dye != nullptr) {
                        if (const std::string* dyed = dye->as_string()) {
                            for (int c = 0; c < 16; ++c) {
                                if (*dyed == kColourNames[c]) {
                                    layer.colour = opaque(kDyeColours[c]);
                                    break;
                                }
                            }
                        }
                    }
                    out.push_back(layer);
                }
            }
        }
        break;
    }
    case BlockEntityKind::Skull: {
        piece.root = skull_pose(root, row).matrix;
        const float degrees = row.rotation >= 0
                                  ? segment_degrees(row.rotation)
                                  : segment_degrees(direction_segment(kOpposite[row.facing]));
        // `SkullModel.setupAnim`, and the two kinds that move more than a head.
        set_rotation(piece.pose, *model, "head", 0.0f, degrees * kRadians, 0.0f);
        const float animation = 0.0f;
        set_rotation(piece.pose, *model, "head/jaw",
                     (jmath::sin(animation * kPi * 0.2f) + 1.0f) * 0.2f, 0.0f, 0.0f);
        const int32_t left = model->find("head/left_ear");
        if (left >= 0) {
            piece.pose[left].z_rot =
                -(jmath::cos(animation * kPi * 0.2f * 1.2f) + 2.5f) * 0.2f;
            const int32_t right = model->find("head/right_ear");
            if (right >= 0) {
                piece.pose[right].z_rot = (jmath::cos(animation * kPi * 0.2f) + 2.5f) * 0.2f;
            }
        }
        piece.type = texture.starts_with("player/") ? gpu::EntityRenderer::Type::Translucent
                                                    : gpu::EntityRenderer::Type::CutoutNoCull;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::ShulkerBox: {
        const float open = entity.last_openness + (entity.openness - entity.last_openness) * partial;
        piece.root = shulker_pose(root, row.facing).matrix;
        const int32_t lid = model->find("lid");
        if (lid >= 0) {
            // `ShulkerBoxRenderer.ShulkerBoxModel.setupAnim`.
            piece.pose[lid].x = 0.0f;
            piece.pose[lid].y = 24.0f - open * 0.5f * 16.0f;
            piece.pose[lid].z = 0.0f;
            piece.pose[lid].y_rot = 270.0f * open * kRadians;
        }
        piece.type = gpu::EntityRenderer::Type::CutoutNoCull;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::Bell:
        // `BellRenderer.submit`: the bell body hangs where the layer puts it.
        piece.root = root.matrix;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(std::move(piece));
        break;
    case BlockEntityKind::Conduit: {
        // `ConduitRenderer.submit` while the conduit is not active: the shell,
        // turning on the spot.
        Placement place = root;
        place.translate(0.5f, 0.5f, 0.5f);
        // `ConduitBlockEntity.getActiveRotation`: the shell only turns once the
        // conduit is active, and an active conduit's cage, wind and eye are
        // not drawn here yet.
        place.rotate(quaternion_y(to_radians(0.0f)));
        piece.root = place.matrix;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::DecoratedPot: {
        // `DecoratedPotRenderer.submit`: the body, then one picture per side.
        Placement turned = root;
        turned.translate(0.5f, 0.0f, 0.5f);
        turned.rotate(quaternion_y(to_radians(180.0f - kYRot[row.facing])));
        turned.translate(-0.5f, 0.0f, -0.5f);
        const Matrix place = turned.matrix;
        piece.root = place;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        for (const char* part : {"neck", "top", "bottom"}) {
            Piece one = piece;
            one.part = part;
            out.push_back(one);
        }
        const Model* sides = models.find("decorated_pot_sides");
        if (sides == nullptr) break;
        const nbt::Value* sherds = entity.data.find("sherds");
        static constexpr const char* kFaces[4] = {"front", "back", "left", "right"};
        for (int face = 0; face < 4; ++face) {
            Piece one;
            one.model = sides;
            one.pose.reset(*sides);
            one.root = place;
            one.part = kFaces[face];
            one.texture = "decorated_pot/decorated_pot_side";
            one.type = gpu::EntityRenderer::Type::Cutout;
            if (sherds != nullptr) {
                if (const nbt::List* list = sherds->as_list()) {
                    const size_t at = static_cast<size_t>(face);
                    if (at < list->size()) {
                        if (const std::string* item = (*list)[at].as_string()) {
                            const size_t colon = item->find(':');
                            std::string_view bare = std::string_view(*item).substr(
                                colon == std::string::npos ? 0 : colon + 1);
                            if (bare != "brick") {
                                one.texture = "decorated_pot/";
                                one.texture.append(bare);
                            }
                        }
                    }
                }
            }
            out.push_back(std::move(one));
        }
        break;
    }
    case BlockEntityKind::EnchantingTable: {
        // `EnchantTableRenderer.submit`: the book bobs and turns above the top.
        const float time = static_cast<float>(entity.ticks) + partial;
        Placement place = root;
        place.translate(0.5f, 0.75f, 0.5f);
        place.translate(0.0f, 0.1f + jmath::sin(time * 0.1f) * 0.01f, 0.0f);
        place.rotate(quaternion_y(0.0f));
        place.rotate(quaternion_z(to_radians(80.0f)));
        piece.root = place.matrix;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        // `BookModel.setupAnim` with the book shut, which is what it is with
        // nobody standing at the table.
        book_pose(piece.pose, *model, time, 0.0f, 0.0f, 0.0f);
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::Lectern: {
        if (row.flag == 0) break;
        // `LecternRenderer.submit`: the book lies open on the stand.
        const float yaw = kYRot[kClockWise[row.facing]];
        Placement place = root;
        place.translate(0.5f, 1.0625f, 0.5f);
        place.rotate(quaternion_y(to_radians(-yaw)));
        place.rotate(quaternion_z(to_radians(67.5f)));
        place.translate(0.0f, -0.125f, 0.0f);
        piece.root = place.matrix;
        piece.type = gpu::EntityRenderer::Type::Cutout;
        // `LecternRenderer.submit`: the one pose a lectern's book ever has.
        book_pose(piece.pose, *model, 0.0f, 0.1f, 0.9f, 1.2f);
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::CopperGolemStatue: {
        // `CopperGolemStatueBlockRenderer.submit`, `CopperGolemStatueModel.setupAnim`.
        Placement place = root;
        place.translate(0.5f, 0.0f, 0.5f);
        piece.root = place.matrix;
        const int32_t rootPart = model->find("root");
        if (rootPart >= 0) {
            piece.pose[rootPart].y = 0.0f;
            piece.pose[rootPart].y_rot = kYRot[kOpposite[row.facing]] * kRadians;
            piece.pose[rootPart].z_rot = kPi;
        }
        piece.type = gpu::EntityRenderer::Type::CutoutNoCull;
        out.push_back(std::move(piece));
        break;
    }
    case BlockEntityKind::None:
        break;
    }
}

void BlockEntityScene::sign_text(const BlockEntityRow& row, const BlockEntity& entity,
                                 const Placement& root, const Font& font,
                                 std::vector<SignLine>& out) {
    const bool hanging = row.kind == BlockEntityKind::HangingSign;
    if (!hanging && row.kind != BlockEntityKind::Sign) return;
    const float line_height = hanging ? 9.0f : 10.0f;
    const float text_scale = hanging ? 0.9f : 0.6666667f;
    const float offset[3] = {0.0f, hanging ? -0.32f : 0.33333334f,
                             hanging ? 0.073f : 0.046666667f};
    const Placement place = sign_pose(root, row, hanging);

    for (int face = 0; face < 2; ++face) {
        const nbt::Value* side = side_text(entity, face == 0);
        if (side == nullptr) continue;
        const nbt::Value* messages = side->find("messages");
        if (messages == nullptr) continue;
        const nbt::List* lines = messages->as_list();
        if (lines == nullptr) continue;

        // `AbstractSignRenderer.translateSignText`.
        Placement m = place;
        if (face == 1) m.rotate(quaternion_y(to_radians(180.0f)));
        m.translate(offset[0], offset[1], offset[2]);
        const float scale = 0.015625f * text_scale;
        m.scale(scale, -scale, scale);

        const bool glowing = [&] {
            const nbt::Value* glow = side->find("has_glowing_text");
            const auto value = glow != nullptr ? glow->as_int() : std::nullopt;
            return value.has_value() && *value != 0;
        }();
        uint32_t text_colour = opaque(kTextColours[15]);
        if (const nbt::Value* dye = side->find("color")) {
            if (const std::string* name = dye->as_string()) {
                for (int c = 0; c < 16; ++c) {
                    if (*name == kColourNames[c]) text_colour = opaque(kTextColours[c]);
                }
            }
        }
        // `AbstractSignRenderer.getDarkColor`: unlit text is the colour at two
        // fifths, and black glowing text is written in one grey instead.
        const uint32_t dark = text_colour == opaque(kTextColours[15]) && glowing
                                  ? 0xFFF0EBCCu
                                  : scale_rgb(text_colour, 0.4f);

        const float middle = 4.0f * line_height / 2.0f;
        for (size_t index = 0; index < lines->size() && index < 4; ++index) {
            std::string text = nbt::component_to_text((*lines)[index]);
            if (text.empty()) continue;
            SignLine out_line;
            out_line.text = std::move(text);
            out_line.colour = glowing ? text_colour : dark;
            out_line.glowing = glowing;
            // `AbstractSignRenderer.submitSignText`: each line is centred.
            Placement line_place = m;
            line_place.translate(0.0f, static_cast<float>(index) * line_height - middle, 0.0f);
            out_line.place = line_place.matrix;
            out.push_back(std::move(out_line));
        }
        (void)font;
    }
}

void BlockEntityScene::draw(gpu::EntityRenderer& renderer, const View& view,
                            const BlockStateAt& state, const LightAt& light, int64_t game_time) {
    const auto started = std::chrono::steady_clock::now();
    stats_ = {};
    stats_.tracked = entities_.size();
    if (table_ == nullptr || models_ == nullptr || font_ == nullptr) return;

    const double reach = 64.0;
    for (auto& [key, entity] : entities_) {
        const double dx = static_cast<double>(entity.x) + 0.5 - view.eye[0];
        const double dy = static_cast<double>(entity.y) + 0.5 - view.eye[1];
        const double dz = static_cast<double>(entity.z) + 0.5 - view.eye[2];
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        const BlockEntityRow* row = table_->find(state(entity.x, entity.y, entity.z));
        if (row == nullptr) continue;

        // `BlockEntityRenderDispatcher.submit` is reached with the block's own
        // corner on the stack, from where the camera stands.
        Placement root;
        root.translate(static_cast<float>(static_cast<double>(entity.x) - view.eye[0]),
                       static_cast<float>(static_cast<double>(entity.y) - view.eye[1]),
                       static_cast<float>(static_cast<double>(entity.z) - view.eye[2]));
        pieces_.clear();
        build(*row, *table_, entity, *models_, root, view.partial, game_time, pieces_);
        if (pieces_.empty()) continue;
        const auto lit = light(entity.x, entity.y, entity.z);

        for (Piece& piece : pieces_) {
            const Model& model = *piece.model;
            if (matrices_.size() < model.parts.size()) matrices_.resize(model.parts.size());
            std::span<Matrix> out(matrices_.data(), model.parts.size());
            if (!piece.part.empty()) {
                // A renderer that submits one part draws that part and what
                // hangs from it; the parts above still carry it, so they skip
                // their own boxes rather than go out.
                const int32_t wanted = model.find(piece.part);
                if (wanted < 0) continue;
                for (size_t i = 0; i < model.parts.size(); ++i) {
                    bool under = static_cast<int32_t>(i) == wanted;
                    for (int32_t at = model.parts[i].parent; at >= 0 && !under;
                         at = model.parts[static_cast<size_t>(at)].parent) {
                        under = at == wanted;
                    }
                    piece.pose[static_cast<int32_t>(i)].skip_draw = !under;
                }
            }
            pose_matrices(model, piece.pose, piece.root, out);

            gpu::EntityRenderer::Instance instance;
            instance.model = renderer.model(model.name);
            if (instance.model < 0) continue;
            uint32_t picture = 0;
            if (row->kind == BlockEntityKind::Skull && piece.texture.starts_with("player/")) {
                const int32_t layer = skin_for(renderer, entity.data);
                if (layer >= 0) picture = gpu::EntityRenderer::kSkinBit |
                                          static_cast<uint32_t>(layer);
            }
            if (picture == 0) {
                const int32_t texture = renderer.texture(piece.texture);
                if (texture < 0) continue;
                picture = static_cast<uint32_t>(texture);
            }
            instance.texture = picture;
            instance.type = piece.type;
            instance.sky_light = lit[0];
            instance.block_light = lit[1];
            instance.colour = piece.colour;
            if (renderer.add(instance, out)) {
                ++stats_.models;
                stats_.parts += out.size();
            }
        }
        ++stats_.drawn;

        if (row->kind == BlockEntityKind::Sign || row->kind == BlockEntityKind::HangingSign) {
            lines_.clear();
            sign_text(*row, entity, root, *font_, lines_);
            for (const SignLine& line : lines_) {
                const uint8_t sky = line.glowing ? 15 : lit[0];
                const uint8_t block = line.glowing ? 15 : lit[1];
                renderer.add_tag(line.text, line.place, false, line.colour, 0, sky, block);
            }
        }
    }
    stats_.pose_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

void scan_chunk(const world::Chunk& chunk, const BlockEntityTable& table,
                BlockEntityScene& scene) {
    for (size_t index = 0; index < chunk.sections.size(); ++index) {
        const world::PalettedContainer& blocks = chunk.sections[index].blocks;
        const std::vector<int32_t>& palette = blocks.palette();
        if (!palette.empty()) {
            // A section whose whole palette is ordinary blocks is skipped
            // without reading a single entry.
            bool any = false;
            for (const int32_t state : palette) {
                if (table.find(state) != nullptr) {
                    any = true;
                    break;
                }
            }
            if (!any) continue;
        }
        const auto base = static_cast<int32_t>(index) * 16;
        for (int32_t y = 0; y < 16; ++y) {
            for (int32_t z = 0; z < 16; ++z) {
                for (int32_t x = 0; x < 16; ++x) {
                    const int32_t state = chunk.block_state(x, base + y, z);
                    if (table.find(state) == nullptr) continue;
                    scene.appear((chunk.x << 4) + x, chunk.min_y + base + y, (chunk.z << 4) + z);
                }
            }
        }
    }
}

} // namespace mcpp::entity
