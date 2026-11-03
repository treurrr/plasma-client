#include "mcpp/entity/model.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/proto/packet_ids.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/skins.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
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
        check(near(e.x, 11.5) && near(e.y, 70.0) && near(e.z, 2.0), "teleport, x relative");
        check(near(e.yaw, 180.0) && near(e.pitch, 10.0), "teleport angles");
        check(near(e.head_yaw, -45.0), "head yaw from rotate_head");
        check(e.vehicle == 8, "rides 8");
    } else {
        check(false, "entity 7 present");
    }
    if (const auto found = entities.find(8); found != entities.end()) {
        check(found->second.passengers == std::vector<int32_t>{7}, "8 carries 7");
    }
    check(events.size() == 1 && events[0] == std::pair<int32_t, uint8_t>{7, 3}, "death event");
    fs::remove(file);
}

void test_model() {
    mcpp::entity::ModelSet set;
    set.parse(R"(
[cube]
texture thing 64 32
part root pivot 0 24 0
  box -4 -8 -4  8 8 8  uv 0 0
part lid parent root pivot 0 -8 0 rotate 90 0 0
  box -1 -1 -1  2 2 2  uv 32 0 inflate 0.5 mirror
)");
    const auto* model = set.find("cube");
    check(model != nullptr && model->parts.size() == 2, "two parts");
    if (model == nullptr) return;
    check(model->parts[1].parent == 0 && near(model->parts[1].rotation[0], 3.14159265 / 2),
          "parent and degrees read as radians");
    check(model->parts[1].boxes[0].mirror && near(model->parts[1].boxes[0].inflate, 0.5),
          "box options");

    mcpp::entity::Pose pose(*model);
    std::vector<mcpp::entity::Vertex> out;
    mcpp::entity::build_mesh(*model, pose, mcpp::entity::identity(), out);
    check(out.size() == 72, "two boxes, 36 vertices each");

    // The front face of the first box uses the island's front rectangle:
    // u from 8 to 16 and v from 8 to 16 on a 64x32 sheet.
    float u_low = 1.0f, u_high = 0.0f, v_low = 1.0f, v_high = 0.0f;
    for (size_t i = 0; i < 6; ++i) {
        u_low = std::min(u_low, out[i].uv[0]);
        u_high = std::max(u_high, out[i].uv[0]);
        v_low = std::min(v_low, out[i].uv[1]);
        v_high = std::max(v_high, out[i].uv[1]);
        check(near(out[i].position[2], -4.0) && near(out[i].normal[2], -1.0), "front at -z");
    }
    check(near(u_low, 8.0 / 64) && near(u_high, 16.0 / 64) && near(v_low, 8.0 / 32) &&
              near(v_high, 16.0 / 32),
          "front face uv");

    pose[0].visible = false;
    out.clear();
    mcpp::entity::build_mesh(*model, pose, mcpp::entity::identity(), out);
    check(out.empty(), "a hidden part hides its children");

    bool threw = false;
    try {
        set.parse("[x]\npart a parent nobody\n");
    } catch (const std::exception& error) {
        threw = std::string_view(error.what()).find("line 2") != std::string_view::npos;
    }
    check(threw, "a bad parent is reported with its line");

    // Standing at the origin facing south, a player's head top is 1.875 up.
    const auto m = mcpp::entity::placement(0.0f, 0.9375f);
    const float top[3] = {0.0f, -8.0f, 0.0f};
    const float y = m[1] * top[0] + m[5] * top[1] + m[9] * top[2] + m[13];
    check(near(y, 1.876, 1e-3), std::format("head top at {:.4f}", y));
    // Its front faces +z, and its own right is towards -x.
    const float front_z = m[2] * 0 + m[6] * 0 + m[10] * -1.0f;
    check(front_z > 0.0f, "front faces south");
    const float right_x = m[0] * -1.0f;
    check(right_x < 0.0f, "right hand to the west");
}

void test_motion() {
    mcpp::Entity entity;
    entity.x = 0.0;
    entity.yaw = 170.0f;
    entity.head_yaw = 170.0f;
    mcpp::entity::Motion motion;
    motion.spawn(entity);
    entity.x = 3.0;
    entity.yaw = -170.0f;
    motion.receive(entity);
    motion.tick();
    check(near(motion.position[0], 1.0), "a third of the way after one tick");
    check(near(motion.yaw, 176.6667, 1e-3), "yaw through the short way round");
    motion.tick();
    motion.tick();
    check(near(motion.position[0], 3.0), "there after three");
    motion.tick();
    check(near(motion.position[0], 3.0) && motion.steps == 0, "and stays");
    check(motion.walk_speed > 0.0f, "walking counted");

    mcpp::entity::TickClock clock;
    check(clock.advance(0.12f) == 2 && near(clock.partial(), 0.4, 1e-3), "two ticks and a bit");
    check(clock.advance(5.0f) == 20, "a stall is capped at a second");
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

} // namespace

int main() {
    test_packets();
    test_model();
    test_motion();
    test_skins();
    if (failures == 0) std::cout << "entities: all passed\n";
    return failures == 0 ? 0 : 1;
}
