#include "audio/attenuation.hpp"
#include "audio/block_sounds.hpp"
#include "audio/categories.hpp"
#include "audio/engine.hpp"
#include "audio/footsteps.hpp"
#include "audio/level_events.hpp"
#include "audio/mixer.hpp"
#include "mcpp/jmath.hpp"
#include "audio/sounds.hpp"

#include "mcpp/assets/object_store.hpp"
#include "mcpp/protocol_versions.hpp"

#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace mcpp::audio;
namespace jmath = mcpp::jmath;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

bool near(double a, double b, double epsilon = 1e-5) { return std::abs(a - b) < epsilon; }

// Every value here is what the JDK's java.util.Random printed for the same
// seed and call (OpenJDK 25), a fresh generator per nextInt(bound).
void test_random_matches_java() {
    struct Row {
        int64_t seed;
        int32_t by_bound[11];
        int32_t next_int;
        int64_t next_long;
        float next_float;
    };
    const int32_t bounds[11] = {1, 2, 3, 4, 6, 7, 10, 16, 23, 100, 1000000007};
    const Row rows[] = {
        {0, {0, 1, 0, 2, 0, 5, 0, 11, 8, 60, 569741353}, -1155484576, -3109364765729502342LL,
         0.6063452f},
        {42, {0, 1, 2, 2, 2, 1, 0, 11, 6, 30, 562431123}, -1170105035, 1008396158678580193LL,
         0.0479393f},
        {-1, {0, 0, 2, 1, 5, 3, 3, 4, 18, 13, 577549913}, 1155099827, 8108487875070533655LL,
         0.5480346f},
        {123456789012345LL, {0, 1, 1, 2, 4, 0, 4, 10, 20, 74, 436980167}, -1421006947,
         -8896214238084425209LL, 0.6091467f},
        {7046029254386353131LL, {0, 0, 0, 0, 3, 0, 3, 2, 14, 23, 381854823}, 763709646,
         -8990857146920312711LL, 0.2315312f},
    };
    for (const auto& row : rows) {
        for (int i = 0; i < 11; ++i) {
            jmath::LegacyRandom random(row.seed);
            check(random.next_int(bounds[i]) == row.by_bound[i],
                  std::format("nextInt({}) for seed {}", bounds[i], row.seed));
        }
        jmath::LegacyRandom random(row.seed);
        check(random.next_int(1 << 30) >= 0, "a power of two bound stays in range");
        random.set_seed(row.seed);
        check(random.next(32) == row.next_int, std::format("nextInt() for seed {}", row.seed));
        check(random.next_long() == row.next_long, std::format("nextLong for seed {}", row.seed));
        check(near(random.next_float(), row.next_float, 1e-6),
              std::format("nextFloat for seed {}", row.seed));
    }
}

constexpr std::string_view kSounds = R"({
    "block.test.step": {
        "sounds": ["step/stone1", {"name": "step/stone2", "weight": 3, "volume": 0.5}],
        "subtitle": "subtitles.block.generic.footsteps"
    },
    "block.test.alias": {
        "sounds": [{"name": "block.test.step", "type": "event", "pitch": 0.5}]
    },
    "block.portal.ambient": {
        "sounds": [{"name": "portal/portal", "attenuation_distance": 10, "stream": true,
                    "preload": true}]
    },
    "music.test": {"sounds": [{"name": "other:music/song", "stream": true}]},
    "block.empty": {"sounds": []},
    "block.test.dangling": {"sounds": ["step/stone1", {"name": "no.such.event", "type": "event"}]},
    "block.test.mixed": {"sounds": [{"name": "block.test.step", "type": "event", "stream": true},
                                    "step/gone"]}
})";

void test_sounds_json() {
    SoundIndex index;
    index.parse(kSounds);
    check(index.size() == 7, "seven events");
    const auto* step = index.find("minecraft:block.test.step");
    check(step != nullptr && step->entries.size() == 2, "both entries of an event");
    check(step && step->subtitle == "subtitles.block.generic.footsteps", "the subtitle key");
    check(step && step->entries[0].weight == 1 && step->entries[1].weight == 3, "weights");
    check(step && near(step->entries[1].volume, 0.5) && near(step->entries[0].volume, 1.0),
          "volume defaults to one");
    check(index.total_weight("block.test.step") == 4, "total weight");
    check(index.total_weight("block.test.alias") == 4, "a reference weighs what its event weighs");
    check(index.total_weight("block.test.dangling") == 1, "a reference to nothing weighs nothing");
    check(index.total_weight("block.test.mixed") == 5, "before the missing file is dropped");
    index.prune([](std::string_view file) { return file != "minecraft/sounds/step/gone.ogg"; });
    check(index.total_weight("block.test.mixed") == 4, "a file the assets lack is dropped");
    {
        jmath::LegacyRandom seeded(3);
        const auto mixed = index.pick("block.test.mixed", seeded);
        check(mixed && mixed->stream, "a reference's stream flag carries over");
    }
    const auto* portal = index.find("block.portal.ambient");
    check(portal && portal->entries[0].attenuation_distance == 10 && portal->entries[0].stream &&
              portal->entries[0].preload,
          "attenuation, stream and preload");
    check(index.find("intentionally_empty") == nullptr, "an undefined event is not there");

    jmath::LegacyRandom random(0);
    check(!index.pick("block.empty", random).has_value(), "an empty event plays nothing");
    random.set_seed(0);
    check(!index.pick("intentionally_empty", random).has_value(), "nor does a missing one");

    // nextInt(4) for these seeds is 2, 2, 0 and 1 (the Java table above):
    // 0 is the first entry, 1-3 the second, weighted three times.
    const struct {
        int64_t seed;
        const char* file;
    } picks[] = {{0, "minecraft/sounds/step/stone2.ogg"},
                 {42, "minecraft/sounds/step/stone2.ogg"},
                 {7046029254386353131LL, "minecraft/sounds/step/stone1.ogg"},
                 {-1, "minecraft/sounds/step/stone2.ogg"}};
    for (const auto& pick : picks) {
        jmath::LegacyRandom seeded(pick.seed);
        const auto sound = index.pick("block.test.step", seeded);
        check(sound && sound->file == pick.file, std::format("weighted pick for seed {}", pick.seed));
    }

    jmath::LegacyRandom seeded(7046029254386353131LL);
    const auto alias = index.pick("block.test.alias", seeded);
    check(alias && alias->event == "block.test.alias", "an alias plays under its own name");
    check(alias && near(alias->pitch, 0.5) && !alias->file.empty(),
          "an alias multiplies into what it points at");

    jmath::LegacyRandom other(1);
    const auto song = index.pick("music.test", other);
    check(song && song->file == "other/sounds/music/song.ogg" && song->stream,
          "a namespace picks the file's directory");
}

void test_attenuation() {
    check(near(distance_gain(0, 16), 1.0), "full at the source");
    check(near(distance_gain(8, 16), 0.5), "half way is half");
    check(near(distance_gain(16, 16), 0.0), "silent at the edge");
    check(near(distance_gain(40, 16), 0.0), "and beyond");
    check(near(audible_range(1.0f, 16), 16), "16 blocks at volume one");
    check(near(audible_range(0.25f, 16), 16), "a quiet sound still reaches 16");
    check(near(audible_range(4.0f, 16), 64), "a record at volume four reaches 64");
    check(near(audible_range(1.0f, 10), 10), "the event's own attenuation distance");
    check(near(clamp_volume(4.0f), 1.0) && near(clamp_volume(-1.0f), 0.0), "volume clamps");
    check(near(clamp_pitch(0.3f), 0.5) && near(clamp_pitch(3.0f), 2.0), "pitch clamps");
    const auto centre = stereo_pan(0.0f);
    check(near(centre.left, centre.right) && near(centre.left * centre.left * 2, 1.0),
          "centre is equal power");
    const auto right = stereo_pan(1.0f);
    check(near(right.left, 0.0) && near(right.right, 1.0), "hard right");
}

void test_categories() {
    CategoryVolumes volumes;
    check(near(volumes.gain(Category::Block), 1.0), "all sliders start at 100%");
    volumes[Category::Master] = 0.5f;
    volumes[Category::Block] = 0.5f;
    check(near(volumes.gain(Category::Block), 0.25), "master times the category");
    check(near(volumes.gain(Category::Master), 0.5), "master counts once for itself");
    check(near(volumes.gain(Category::Hostile), 0.5), "other categories only see master");
    check(category_from_id(10) == Category::Ui && !category_from_id(11), "eleven categories");
    check(category_from_name("record") == Category::Record, "by options name");
    check(category_name(Category::Player) == "player", "the options.txt spelling");
}

void test_block_sounds() {
    const auto& table = BlockSounds::builtin();
    check(table.size() == 1166, std::format("one row per block of 1.21.11, got {}", table.size()));
    const auto& stone = table.find("minecraft:stone");
    check(stone.name == "stone" && stone.step == "block.stone.step", "stone");
    const auto& planks = table.find("oak_planks");
    check(planks.name == "wood" && planks.break_sound == "block.wood.break", "planks are wood");
    check(table.find("white_carpet").name == "wool", "carpet is wool");
    check(table.find("minecraft:nonexistent").name == "stone", "unknown sounds like stone");
    const auto& anvil = table.find("anvil");
    check(near(anvil.volume, 0.3), "the anvil is quiet");
    // The wiki's own adjusted numbers for the anvil: 0.65 / 0.8 broken and
    // placed, 0.1625 / 0.5 while digging, 0.15 / 0.75 falling, 0.045 / 1 stepping.
    const auto broken = cue(anvil, BlockSoundKind::Break);
    check(broken && near(broken->volume, 0.65) && near(broken->pitch, 0.8), "anvil break");
    const auto hit = cue(anvil, BlockSoundKind::Hit);
    check(hit && near(hit->volume, 0.1625) && near(hit->pitch, 0.5), "anvil hit");
    const auto fall = cue(anvil, BlockSoundKind::Fall);
    check(fall && near(fall->volume, 0.15) && near(fall->pitch, 0.75), "anvil fall");
    const auto step = cue(anvil, BlockSoundKind::Step);
    check(step && near(step->volume, 0.045) && near(step->pitch, 1.0), "anvil step");
    check(!cue(table.find("cactus_flower"), BlockSoundKind::Step), "no event, no sound");
    check(table.find("water").step == "intentionally_empty", "fluids are silent");
}

void test_level_events() {
    LevelEvent event;
    event.x = -3000;
    event.y = 150;
    event.z = 3000;
    event.type = 1031;
    // new java.util.Random(42).nextFloat() is 0.7275637: the anvil lands at 0.3
    // and 0.9 plus a tenth of that.
    jmath::LegacyRandom random(42);
    auto anvil = level_event_sound(event, random);
    check(anvil && anvil->event == "block.anvil.land" && anvil->category == Category::Block &&
              near(anvil->volume, 0.3) && near(anvil->pitch, 0.7275637f * 0.1f + 0.9f) &&
              near(anvil->x, -2999.5) && near(anvil->y, 150.5),
          "the anvil landing");

    event.type = 1015;
    random.set_seed(42);
    jmath::LegacyRandom twin(42);
    const float a = twin.next_float();
    const float b = twin.next_float();
    const auto warn = level_event_sound(event, random);
    check(warn && near(warn->volume, 10.0) && near(warn->pitch, (a - b) * 0.2f + 1.0f),
          "the ghast's warning: the first float less the second");

    event.type = 2001;
    event.data_block = "minecraft:oak_planks";
    const auto broken = level_event_sound(event, random);
    check(broken && broken->event == "block.wood.break" && near(broken->volume, 1.0) &&
              near(broken->pitch, 0.8),
          "a block breaking plays its own type");
    event.data_block = "minecraft:air";
    check(!level_event_sound(event, random), "air breaks silently");

    event.type = 3012;
    event.camera[0] = -2999.5 + 80.0;
    event.camera[1] = 150.5;
    event.camera[2] = 3000.5;
    const auto far = level_event_sound(event, random);
    check(far && far->delay == 40, "80 blocks away the trial spawner is heard two seconds late");

    event.type = 1028;
    event.camera[0] = 0.0;
    event.camera[1] = 64.0;
    event.camera[2] = 0.0;
    event.x = 99;
    event.y = 63;
    event.z = -1;
    const auto death = global_level_event_sound(event);
    check(death && near(death->x, 2.0, 1e-3) && near(death->y, 64.0, 0.02) && near(death->volume, 5.0),
          "the dragon's death two blocks off towards it");
    event.type = 2010;
    check(!level_event_sound(event, random), "particles only");
    check(jukebox_sound_event("minecraft:pigstep", nullptr) == "music_disc.pigstep", "a record");
}

std::shared_ptr<Pcm> tone(int channels, size_t frames, int16_t value) {
    auto pcm = std::make_shared<Pcm>();
    pcm->channels = channels;
    pcm->rate = 48000;
    pcm->samples.assign(frames * static_cast<size_t>(channels), value);
    return pcm;
}

void test_mixer() {
    Mixer mixer;
    std::vector<float> out(64 * 2);
    std::vector<uint32_t> released;
    mixer.start(0, tone(1, 100, 16384), 1.0f, 0.5f, 0.25f, 1.0f, false);
    mixer.mix(out.data(), 64);
    check(near(out[0], 0.25) && near(out[1], 0.125), "gain per side");
    mixer.mix(out.data(), 64);
    check(near(out[2 * 35], 0.25) && near(out[2 * 40], 0.0), "ends after its frames");
    mixer.released(released);
    check(released.size() == 1 && released[0] == 0, "and hands its slot back");

    released.clear();
    mixer.start(1, tone(1, 100, 16384), 2.0f, 1.0f, 1.0f, 1.0f, false);
    mixer.mix(out.data(), 64);
    check(near(out[2 * 49], 0.5) && near(out[2 * 51], 0.0), "pitch two plays twice as fast");
    mixer.released(released);

    released.clear();
    mixer.start(2, tone(2, 10, 8192), 1.0f, 0.0f, 0.0f, 1.0f, true);
    mixer.mix(out.data(), 64);
    mixer.released(released);
    check(near(out[2 * 63], 0.25) && released.empty(),
          "a loop keeps going, and two channels play at the flat gain");
    mixer.stop(2);
    mixer.mix(out.data(), 64);
    mixer.released(released);
    check(released.size() == 1 && released[0] == 2 && near(out[0], 0.0), "stopped");

    // A gain change glides over one callback instead of jumping.
    released.clear();
    mixer.start(3, tone(1, 1000, 16384), 1.0f, 0.0f, 0.0f, 0.0f, false);
    mixer.mix(out.data(), 64);
    mixer.set(3, 1.0f, 1.0f, 1.0f, 1.0f);
    mixer.mix(out.data(), 64);
    check(near(out[0], 0.0) && near(out[2 * 32], 0.25) && out[2 * 63] < 0.5f,
          "a new gain is reached by the end of the callback");
    mixer.stop(3);
    mixer.released(released);

    // The null device pulls the stream at the real rate and plays nothing.
    Mixer null;
    Mixer::Options options;
    options.null_output = true;
    check(null.open(options), "the null device opens");
    null.start(7, tone(1, 2400, 1000), 1.0f, 1.0f, 1.0f, 1.0f, false);
    bool finished = false;
    for (int i = 0; i < 100 && !finished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        released.clear();
        null.released(released);
        for (const auto slot : released) finished |= slot == 7;
    }
    check(finished, "a 50 ms sound ends on the null device");
    check(null.stats().callbacks > 0, "the device called the mixer");
}

// The engine against the player's own files, when the launcher has them.
void test_engine_on_assets() {
    const auto store = mcpp::assets::ObjectStore::for_version(mcpp::protocol::kTargetVersionName);
    const auto text = store ? store->read("minecraft/sounds.json") : std::nullopt;
    if (!text) {
        std::cout << "skipped: no 1.21.11 assets in a launcher\n";
        return;
    }
    SoundIndex index;
    index.parse(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
    check(index.size() == 1837, std::format("1837 events in sounds.json, got {}", index.size()));
    index.prune([&](std::string_view name) { return store->find(name) != nullptr; });
    // Each row is what an independent Java implementation of the game's
    // weighted pick, run on java.util.Random over the same sounds.json, chose.
    // music.creative mixes references to music.game with its own tracks, so
    // it only comes out right when a reference weighs its whole event.
    const struct {
        const char* event;
        int64_t seed;
        int32_t total;
        const char* file;
        float volume;
        float pitch;
        bool stream;
    } oracle[] = {
        {"music.creative", 0LL, 33, "music/game/wet_hands", 1.0f, 1.0f, true},
        {"music.creative", 42LL, 33, "music/game/creative/blind_spots", 1.0f, 1.0f, true},
        {"music.creative", -4490259591351732256LL, 33, "music/game/dry_hands", 1.0f, 1.0f, true},
        {"music.creative", 7LL, 33, "music/game/broken_clocks", 0.4f, 1.0f, true},
        {"block.note_block.imitate.creeper", 7LL, 1, "random/fuse", 1.0f, 0.5f, false},
        {"ambient.basalt_deltas.additions", 123456789LL, 574,
         "ambient/nether/basalt_deltas/long_debris1", 0.35f, 1.0f, false},
        {"ambient.basalt_deltas.additions", -1LL, 574, "ambient/nether/basalt_deltas/debris3",
         0.35f, 1.0f, false},
        {"block.anvil.land", 5LL, 1, "random/anvil_land", 0.5f, 1.0f, false},
        {"block.cave_vines.pick_berries", 99LL, 2, "item/sweet_berries/pick_from_bush1", 1.0f,
         1.0f, false},
        {"ambient.cave", -2678199317400689785LL, 23, "ambient/cave/cave6", 1.0f, 1.0f, false},
        {"block.stone.step", -7939870404668536451LL, 6, "step/stone4", 1.0f, 1.0f, false},
        {"intentionally_empty", 1LL, 0, nullptr, 0.0f, 0.0f, false},
    };
    for (const auto& row : oracle) {
        jmath::LegacyRandom seeded(row.seed);
        const auto sound = index.pick(row.event, seeded);
        check(index.total_weight(row.event) == row.total,
              std::format("{} weighs {}", row.event, row.total));
        if (row.file == nullptr) {
            check(!sound, std::format("{} plays nothing", row.event));
            continue;
        }
        check(sound && sound->file == std::format("minecraft/sounds/{}.ogg", row.file) &&
                  near(sound->volume, row.volume) && near(sound->pitch, row.pitch) &&
                  sound->stream == row.stream,
              std::format("{} with seed {} picks {}, got {}", row.event, row.seed, row.file,
                          sound ? sound->file : std::string("nothing")));
    }
    size_t subtitles = 0;
    for (const auto* name : {"block.anvil.land", "block.note_block.harp", "ambient.cave"}) {
        check(index.find(name) != nullptr, name);
    }
    (void)subtitles;

    SoundEngine engine;
    Mixer::Options options;
    options.no_device = true;
    engine.open(std::move(index), [&](std::string_view name) { return store->read(name); },
                options);
    std::vector<VoiceLog> lines;
    engine.on_log = [&](const VoiceLog& line) { lines.push_back(line); };
    engine.set_listener({0.0, 64.0, 0.0, 0.0f, 0.0f});
    engine.volumes[Category::Block] = 0.5f;

    PlayRequest request;
    request.event = "minecraft:block.anvil.land";
    request.category = Category::Block;
    request.x = 8.0;
    request.y = 64.0;
    request.z = 0.0;
    request.volume = 1.0f;
    request.pitch = 1.0f;
    jmath::LegacyRandom random(5);
    const auto id = engine.play(request, random);
    check(id != 0 && !lines.empty() && lines.back().what == "start", "the anvil starts");
    if (!lines.empty()) {
        const auto& line = lines.back();
        // sounds.json gives the anvil 0.5; the slider halves it; 8 of 16 blocks halves it again.
        check(near(line.gain, 0.125, 1e-4), std::format("anvil gain {}", line.gain));
        check(near(line.distance, 8.0) && near(line.range, 16.0), "distance and range");
        // Facing south, +x is to the left.
        check(line.left > line.right, "a sound to the east is on the left when facing south");
    }
    request.x = 40.0;
    engine.play(request, random);
    check(!lines.empty() && near(lines.back().gain, 0.0), "out of range is silent");
    engine.volumes[Category::Block] = 1.0f;
    engine.stop(Category::Block, "");
    check(engine.active() == 0, "stop by category");

    engine.play(PlayRequest::ui("ui.button.click", 1.0f), random);
    check(!lines.empty() && lines.back().what == "start" && near(lines.back().gain, 0.25) &&
              near(lines.back().left, lines.back().right),
          "the interface plays centred, never attenuated");

    // sounds.json makes the bell 12 loud; rung at 2 that is 24, which carries
    // 24 times 16 blocks and is no louder for it.
    request = {};
    request.event = "block.bell.use";
    request.category = Category::Block;
    request.x = 20.0;
    request.y = 64.0;
    request.volume = 2.0f;
    engine.play(request, random);
    check(!lines.empty() && near(lines.back().range, 384.0) &&
              near(lines.back().gain, 1.0 - 20.0 / 384.0, 1e-4),
          std::format("bell range {} gain {}", lines.empty() ? 0.0f : lines.back().range,
                      lines.empty() ? 0.0f : lines.back().gain));

    engine.volumes[Category::Hostile] = 0.0f;
    request.event = "entity.zombie.ambient";
    request.category = Category::Hostile;
    check(engine.play(request, random) == 0 && lines.back().what == "silent",
          "a sound whose slider is at zero never starts");
    engine.volumes[Category::Music] = 0.0f;
    check(engine.play(PlayRequest::music("music.creative"), random) != 0 &&
              lines.back().what == "start" && lines.back().stream,
          "music starts anyway, as a stream");
    engine.stop(std::nullopt, "minecraft:music.creative");
    check(lines.back().what == "stop" && lines.back().event == "music.creative", "stop by name");

    request.event = "block.stone.step";
    request.category = Category::Player;
    request.delay = 3;
    engine.play(request, random);
    check(lines.back().what == "delay", "a delayed sound waits");
    engine.update();
    engine.update();
    check(lines.back().what == "delay", "for its ticks");
    engine.update();
    check(lines.back().what == "start" && lines.back().event == "block.stone.step", "then plays");
    request.delay = 0;

    engine.stop(std::nullopt, "");
    engine.update();
    size_t started = 0;
    size_t dropped = 0;
    engine.on_log = [&](const VoiceLog& line) {
        started += line.what == "start";
        dropped += line.what == "drop";
    };
    for (int i = 0; i < 300; ++i) engine.play(request, random);
    check(started == SoundEngine::kStaticVoices && dropped == 300 - SoundEngine::kStaticVoices,
          std::format("247 at once, the rest turned away: {} started, {} dropped", started, dropped));
    engine.stop(std::nullopt, "");
    engine.update();

    request.entity = 9;
    engine.on_log = [&](const VoiceLog& line) { lines.push_back(line); };
    engine.play(request, random);
    engine.update([](int32_t id) -> std::optional<std::array<double, 3>> {
        if (id == 9) return std::array<double, 3>{1.0, 64.0, 1.0};
        return std::nullopt;
    });
    check(engine.active() == 1, "a sound on an entity plays while it is there");
    engine.update([](int32_t) -> std::optional<std::array<double, 3>> { return std::nullopt; });
    check(engine.active() == 0 && lines.back().what == "stop", "and stops when it goes");
    std::cout << std::format("engine: {} sounds decoded in {:.1f} ms, worst {:.2f} ms ({}), cache {} KiB\n",
                             engine.stats().decodes, engine.stats().decode_ms,
                             engine.stats().worst_decode_ms, engine.stats().worst_decode,
                             engine.stats().cache_bytes / 1024);
}

} // namespace

int main() {
    test_random_matches_java();
    test_sounds_json();
    test_attenuation();
    test_categories();
    test_block_sounds();
    test_level_events();
    test_mixer();
    test_engine_on_assets();
    if (failures == 0) std::cout << "all sound tests passed\n";
    return failures == 0 ? 0 : 1;
}
