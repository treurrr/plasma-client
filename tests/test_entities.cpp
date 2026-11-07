#include "mcpp/entity/model.hpp"
#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/skins.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a - b) <= tolerance; }

// A recording made by hand: the session reads it exactly as it reads one
// written off a real server, so every handler sees these bytes as packets.
class Recording {
public:
    explicit Recording(const fs::path& path) : out_(path, std::ios::binary) { out_.write("PLRC", 4); }
    void add(uint8_t state, int32_t id, const mcpp::ByteBuffer& body) {
        const auto& bytes = body.data();
        const auto length = static_cast<uint32_t>(bytes.size());
        out_.write(reinterpret_cast<const char*>(&state), 1);
        out_.write(reinterpret_cast<const char*>(&id), 4);
        out_.write(reinterpret_cast<const char*>(&length), 4);
        out_.write(reinterpret_cast<const char*>(bytes.data()), length);
    }

private:
    std::ofstream out_;
};

void write_uuid(mcpp::ByteBuffer& out, uint64_t high, uint64_t low) {
    out.write_i64(static_cast<int64_t>(high));
    out.write_i64(static_cast<int64_t>(low));
}

void test_packets() {
    const auto& ids = *mcpp::proto::ids_for(mcpp::protocol::kTargetProtocol);
    const auto& cb = ids.play_cb;
    const fs::path file = fs::temp_directory_path() / "plasma-test-entities.plrc";
    {
        Recording recording(file);
        recording.add(1, ids.config_cb.finish_configuration, mcpp::ByteBuffer{});

        mcpp::ByteBuffer list;
        list.write_u8(0xFF);   // every action
        list.write_varint(1);
        write_uuid(list, 0x0123456789abcdefull, 0xfedcba9876543210ull);
        list.write_string("PlasmaD2");
        list.write_varint(1);
        list.write_string("textures");
        list.write_string("e30=");
        list.write_bool(true);
        list.write_string("c2lnbmVk");
        list.write_bool(false);   // no chat session
        list.write_varint(1);     // creative
        list.write_bool(true);
        list.write_varint(42);
        list.write_bool(false);   // no display name
        list.write_varint(3);
        list.write_bool(false);   // hat hidden
        recording.add(2, cb.player_info_update, list);

        mcpp::ByteBuffer add;
        add.write_varint(7);
        write_uuid(add, 0x0123456789abcdefull, 0xfedcba9876543210ull);
        add.write_varint(155);
        add.write_f64(10.5);
        add.write_f64(64.0);
        add.write_f64(-3.25);
        add.write_u8(0);         // at rest
        add.write_u8(0);         // pitch
        add.write_u8(64);        // yaw, a quarter turn
        add.write_u8(64);        // head
        add.write_varint(0);
        recording.add(2, cb.add_entity, add);

        mcpp::ByteBuffer head;
        head.write_varint(7);
        head.write_u8(static_cast<uint8_t>(-32));
        recording.add(2, cb.rotate_head, head);

        mcpp::ByteBuffer teleport;
        teleport.write_varint(7);
        for (const double v : {1.0, 70.0, 2.0, 0.0, 0.0, 0.0}) teleport.write_f64(v);
        teleport.write_f32(180.0f);
        teleport.write_f32(10.0f);
        teleport.write_i32(1 << 0);   // x is relative
        teleport.write_bool(true);
        recording.add(2, cb.teleport_entity, teleport);

        mcpp::ByteBuffer step;
        step.write_varint(7);
        step.write_u16(static_cast<uint16_t>(int16_t{4096}));
        step.write_u16(0);
        step.write_u16(static_cast<uint16_t>(int16_t{-1}));
        step.write_bool(true);
        recording.add(2, cb.move_entity_pos, step);

        mcpp::ByteBuffer rider;
        rider.write_varint(8);
        write_uuid(rider, 1, 2);
        rider.write_varint(1);
        for (int i = 0; i < 3; ++i) rider.write_f64(0.0);
        for (int i = 0; i < 4; ++i) rider.write_u8(0);
        rider.write_varint(0);
        recording.add(2, cb.add_entity, rider);

        mcpp::ByteBuffer passengers;
        passengers.write_varint(8);
        passengers.write_varint(1);
        passengers.write_varint(7);
        recording.add(2, cb.set_passengers, passengers);

        mcpp::ByteBuffer event;
        event.write_i32(7);
        event.write_u8(3);
        recording.add(2, cb.entity_event, event);

        mcpp::ByteBuffer remove;
        remove.write_varint(1);
        write_uuid(remove, 1, 2);
        recording.add(2, cb.player_info_remove, remove);
    }

    std::vector<std::string> lines;
    std::vector<std::pair<int32_t, uint8_t>> events;
    mcpp::SessionOptions options;
    options.replay = file;
    options.username = "PlasmaD";
    mcpp::SessionEvents handlers;
    handlers.on_status = [&](std::string_view line) { lines.emplace_back(line); };
    handlers.entities.on_event = [&](int32_t id, uint8_t status) { events.emplace_back(id, status); };
    std::vector<mcpp::EntityMove> moves;
    handlers.entities.on_moved = [&](const mcpp::EntityMove& move) { moves.push_back(move); };
    mcpp::Session session(options, handlers);
    session.connect();
    while (session.pump()) {
    }

    for (const auto& line : lines) {
        check(line.find(" read ") == std::string::npos, "no packet half read: " + line);
        check(line.find("not understood") == std::string::npos, line);
    }
    const auto* info = session.player("01234567-89ab-cdef-fedc-ba9876543210");
    check(info != nullptr, "player list line kept by uuid");
    if (info != nullptr) {
        check(info->name == "PlasmaD2", "name");
        check(info->properties.size() == 1 && info->properties[0].signature == "c2lnbmVk",
              "property with signature");
        check(info->game_mode == 1 && info->listed && info->latency == 42, "mode, listed, latency");
        check(info->list_priority == 3 && !info->show_hat, "priority and hat");
    }
    const auto& entities = session.entities();
    check(entities.size() == 2, "two entities");
    if (const auto found = entities.find(7); found != entities.end()) {
        const auto& e = found->second;
        check(mcpp::uuid_string(e.uuid) == "01234567-89ab-cdef-fedc-ba9876543210",
              "uuid string matches the player list's");
        // A step counts from where the last step left it, not from the
        // teleport: x 10.5 plus one block, z -3.25 less one 4096th.
        check(e.x == 11.5 && e.y == 64.0 && e.z == -3.25 - 1.0 / 4096, "step from the base");
        check(near(e.yaw, 180.0) && near(e.pitch, 10.0), "teleport angles");
        check(near(e.head_yaw, -45.0), "head yaw from rotate_head");
        check(e.vehicle == 8, "rides 8");
    } else {
        check(false, "entity 7 present");
    }
    if (const auto found = entities.find(8); found != entities.end()) {
        check(found->second.passengers == std::vector<int32_t>{7}, "8 carries 7");
    }
    check(moves.size() == 2 && moves[0].absolute && moves[0].x == 11.5 && moves[0].z == 2.0,
          "teleport, x relative");
    check(moves.size() == 2 && moves[1].has_position && !moves[1].has_rotation &&
              !moves[1].absolute,
          "a step carries a position only");
    check(events.size() == 1 && events[0] == std::pair<int32_t, uint8_t>{7, 3}, "death event");
    fs::remove(file);
}

const char* kTwoBoxes = R"(
[thing]
texture 64 32
part root
part body pivot 0 12 0 rotate 0.5 0 0
  box -4 0 -2  8 12 4  uv 16 16
part body/arm pivot 5 2 0
  box -1 -2 -2  4 12 4  uv 40 16 grow 0.25 mirror faces ns
)";

void test_model() {
    mcpp::entity::ModelSet set;
    set.parse(kTwoBoxes);
    const auto* model = set.find("thing");
    check(model != nullptr && model->parts.size() == 3, "three parts, root first");
    if (model == nullptr || model->parts.size() != 3) return;
    check(model->texture_width == 64 && model->texture_height == 32, "texture size");
    check(model->find("body/arm") == 2 && model->parts[2].parent == 1, "child after its parent");
    check(model->parts[1].parent == 0, "top parts hang from the root");
    const auto& arm = model->parts[2].boxes[0];
    check(arm.mirror && arm.faces == 0x0C && near(arm.grow[2], 0.25), "box flags");

    // The game's cube: 6 faces in the order down, up, west, north, east,
    // south, two triangles each; the body's first vertex is the down face's
    // first corner, (max x, min y, max z), at the island's top right of the
    // bottom rectangle.
    std::vector<mcpp::entity::Vertex> vertices;
    check(mcpp::entity::bake(*model, vertices) == 36 + 12, "36 vertices, and 12 for two faces");
    const auto& v = vertices[0];
    check(near(v.position[0], 4.0 / 16) && near(v.position[1], 0.0) &&
              near(v.position[2], 2.0 / 16),
          "first corner of the down face");
    check(near(v.uv[0], (16 + 4 + 8) / 64.0) && near(v.uv[1], 16 / 32.0), "its uv");
    check(v.normal[1] == -127 && v.part == 1, "down normal, body part");
    // The mirrored arm swaps x and reverses each face; its north face is
    // the first it has.
    const auto& m = vertices[36];
    check(m.part == 2 && m.normal[2] == -127, "mirrored arm's north face");
    check(near(m.position[0], (-1 - 0.25) / 16.0), "mirrored corner takes the other x");

    mcpp::entity::Pose pose(*model);
    std::vector<mcpp::entity::Matrix> matrices(3);
    pose.parts[2].visible = false;
    mcpp::entity::pose_matrices(*model, pose, mcpp::entity::identity(), matrices);
    check(near(matrices[1][13], 12.0 / 16) && near(matrices[1][5], std::cos(0.5)),
          "pivot then rotation");
    check(matrices[2][15] == 0.0f, "a hidden part draws nothing");
}

void test_motion() {
    mcpp::Entity entity;
    entity.yaw = 170.0f;
    entity.head_yaw = 170.0f;
    mcpp::entity::Living living;
    living.spawn(entity);
    const double to[3] = {3.0, 0.0, 0.0};
    const float yaw = -170.0f;
    const float pitch = 0.0f;
    living.move_to(to, &yaw, &pitch);
    living.tick_remote_player();
    check(near(living.x, 1.0), "a third of the way after one tick");
    check(near(living.y_rot, 176.6667, 1e-3), "yaw through the short way round");
    check(near(living.xo, 0.0), "the old position is the last tick's");
    living.tick_remote_player();
    check(near(living.x, 2.0), "half the rest after two");
    living.tick_remote_player();
    check(near(living.x, 3.0) && living.steps == 0, "there after three");
    living.tick_remote_player();
    check(near(living.x, 3.0), "and stays");
    // Moving a block a tick: the walk eases toward full speed by 0.4.
    check(living.walk.speed > 0.0f, "walking counted");

    // The body turns three tenths of the way and never lets the head more
    // than fifty degrees round.
    mcpp::entity::Living still;
    entity.yaw = 0.0f;
    entity.head_yaw = 0.0f;
    still.spawn(entity);
    still.y_rot = 90.0f;
    still.tick_remote_player();
    check(near(still.y_body_rot, 40.0), "the body pulled to fifty off the head");
    still.tick_remote_player();
    check(near(still.y_body_rot, 40.0), "and no further while standing");

    mcpp::entity::Living swinger;
    swinger.swing(false);
    swinger.tick_remote_player();
    check(swinger.swing_time == 0 && near(swinger.attack_anim, 0.0), "swing starts at -1");
    swinger.tick_remote_player();
    check(near(swinger.attack_anim, 1.0 / 6.0), "a sixth a tick");
    for (int i = 0; i < 5; ++i) swinger.tick_remote_player();
    check(!swinger.swinging && near(swinger.attack_anim, 0.0), "over after six");

    mcpp::entity::TickClock clock;
    check(clock.advance(0.12f) == 2 && near(clock.partial(), 0.4, 1e-3), "two ticks and a bit");
    check(clock.advance(5.0f) == 10, "a stall runs ten ticks at most");
}

void test_skins() {
    check(mcpp::skins::base64_decode("UGxhc21h") == std::vector<uint8_t>{'P', 'l', 'a', 's', 'm', 'a'},
          "base64");
    const auto textures = mcpp::skins::decode_property(
        "eyJ0ZXh0dXJlcyI6eyJTS0lOIjp7InVybCI6Imh0dHA6Ly90ZXh0dXJlcy5taW5lY3JhZnQubmV0L3RleHR1cmUv"
        "YWJjIiwibWV0YWRhdGEiOnsibW9kZWwiOiJzbGltIn19fX0=");
    check(textures && textures->skin_url == "http://textures.minecraft.net/texture/abc" &&
              textures->slim,
          "textures property");

    const auto picked = mcpp::skins::default_skin("01234567-89ab-cdef-fedc-ba9876543210");
    check(picked.texture.starts_with("player/") && picked.slim == picked.texture.starts_with(
                                                                     "player/slim/"),
          "a default skin");

    // A 64x32 skin with a red right leg comes out 64x64 with a red left leg.
    mcpp::entity::Texture legacy;
    legacy.width = 64;
    legacy.height = 32;
    legacy.rgba.assign(64 * 32 * 4, 0);
    for (int y = 20; y < 32; ++y) {
        for (int x = 4; x < 8; ++x) {
            uint8_t* p = &legacy.rgba[(size_t(y) * 64 + size_t(x)) * 4];
            p[0] = 255;
            p[3] = 255;
        }
    }
    check(mcpp::skins::normalise(legacy) && legacy.height == 64, "legacy skin grows");
    const uint8_t* left_front = &legacy.rgba[(size_t(55) * 64 + 21) * 4];
    check(left_front[0] == 255 && left_front[3] == 255, "left leg front mirrored from the right");
    check(legacy.rgba[(size_t(5) * 64 + 5) * 4 + 3] == 255, "head made opaque");

    // A two by one png, one red pixel and one see-through, filtered with Sub.
    std::vector<uint8_t> raw = {1, 255, 0, 0, 255, 1, 0, 0, 0};
    raw[5] = static_cast<uint8_t>(0 - 255);
    raw[6] = 0;
    raw[7] = 0;
    raw[8] = static_cast<uint8_t>(0 - 255);
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> packed(packed_size);
    compress(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()));
    packed.resize(packed_size);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const auto chunk = [&](std::string_view type, const std::vector<uint8_t>& body) {
        const auto length = static_cast<uint32_t>(body.size());
        for (int shift = 24; shift >= 0; shift -= 8) png.push_back(uint8_t(length >> shift));
        png.insert(png.end(), type.begin(), type.end());
        png.insert(png.end(), body.begin(), body.end());
        for (int i = 0; i < 4; ++i) png.push_back(0);
    };
    chunk("IHDR", {0, 0, 0, 2, 0, 0, 0, 1, 8, 6, 0, 0, 0});
    chunk("IDAT", packed);
    chunk("IEND", {});
    mcpp::entity::Texture decoded;
    check(mcpp::skins::decode_png(png, decoded) && decoded.width == 2 && decoded.height == 1,
          "png size");
    check(decoded.rgba == std::vector<uint8_t>{255, 0, 0, 255, 0, 0, 0, 0}, "png pixels");
}

// `DeltaTracker.Timer.advanceGameTime`: the clock keeps what is left of a tick
// after a frame. Seven-millisecond frames are 0.14 of a tick each, so a
// thousand of them are exactly 7 s, which is 140 ticks; a clock that threw the
// remainder away at every tick would run one tick per eight frames and reach
// 125. A single frame of 260 ms is five whole ticks with a fifth of one left.
void test_clock() {
    mcpp::entity::TickClock clock;
    int total = 0;
    for (int frame = 0; frame < 1000; ++frame) total += clock.advance(0.007f);
    check(total == 140, "a thousand 7 ms frames are 140 ticks");
    check(clock.ticks() == 140, "and the clock counted them");
    mcpp::entity::TickClock slow;
    check(slow.advance(0.26f) == 5, "a 260 ms frame is five ticks");
    check(near(static_cast<double>(slow.partial()), 0.2, 1e-5),
          "with a fifth of a tick left over");
    mcpp::entity::TickClock stalled;
    check(stalled.advance(2.0f) == 10, "a long stall runs ten ticks at most");
}

// `RangedAttribute.sanitizeValue`: every attribute of the game is registered
// with a range and its value stops inside it. The ranges here are the ones
// Attributes registers: movement_efficiency [0,1], gravity [-1,1], max_health
// [1,1024], scale [0.0625,16], step_height [0,10].
void test_attribute_range() {
    check(mcpp::sanitize_attribute("minecraft:movement_efficiency", 2.0) == 1.0,
          "movement efficiency stops at one");
    check(mcpp::sanitize_attribute("minecraft:gravity", -4.0) == -1.0,
          "gravity stops at minus one");
    check(mcpp::sanitize_attribute("minecraft:gravity", 0.08) == 0.08,
          "and a value inside the range is left alone");
    check(mcpp::sanitize_attribute("minecraft:max_health", 0.0) == 1.0,
          "no maximum health at all becomes one");
    check(mcpp::sanitize_attribute("minecraft:scale", 0.0) == 0.0625, "scale has a floor");
    check(mcpp::sanitize_attribute("minecraft:step_height", 50.0) == 10.0,
          "a step is at most ten blocks");
    check(mcpp::sanitize_attribute("minecraft:gravity",
                                   std::numeric_limits<double>::quiet_NaN()) == -1.0,
          "NaN becomes the lowest value of the range");
    check(mcpp::sanitize_attribute("plasma:not_an_attribute", 1.0e9) == 1.0e9,
          "an attribute with no known range is left as it came");

    // `AttributeInstance.calculateValue` over the three operations and then the
    // range: base 0 with +0.5 added is 0.5, one times the sum added on top is
    // 1.0, and half of the whole on top of it is 1.5 - which movement
    // efficiency's range cuts back to 1.
    mcpp::Attribute a;
    a.base = 0.0;
    a.modifiers.push_back({"plasma:add", 0.5, 0});
    a.modifiers.push_back({"plasma:base", 1.0, 1});
    a.modifiers.push_back({"plasma:total", 0.5, 2});
    check(near(a.value(), 1.5), "the three operations in the game's order");
    check(mcpp::sanitize_attribute("minecraft:movement_efficiency", a.value()) == 1.0,
          "and the range after them");
}

} // namespace

int main() {
    test_packets();
    test_clock();
    test_attribute_range();
    test_model();
    test_motion();
    test_skins();
    if (failures == 0) std::cout << "entities: all passed\n";
    return failures == 0 ? 0 : 1;
}
