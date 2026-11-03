#include "audio/block_sounds.hpp"

#include "audio/sounds.hpp"
#include "mcpp/protocol_versions.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace mcpp::audio {

namespace {

std::vector<std::string_view> split(std::string_view line, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t end = line.find(separator, start);
        parts.push_back(line.substr(start, end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

float to_float(std::string_view text) {
    float value = 1.0f;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::string event_or_empty(std::string_view text) {
    return text == "-" ? std::string() : std::string(text);
}

} // namespace

std::optional<BlockSoundCue> cue(const BlockSoundType& type, BlockSoundKind kind) {
    const float v = type.volume;
    const float p = type.pitch;
    BlockSoundCue result;
    switch (kind) {
    case BlockSoundKind::Break:
        result = {type.break_sound, (v + 1.0f) / 2.0f, p * 0.8f};
        break;
    case BlockSoundKind::Place:
        result = {type.place, (v + 1.0f) / 2.0f, p * 0.8f};
        break;
    case BlockSoundKind::Hit:
        result = {type.hit, (v + 1.0f) / 8.0f, p * 0.5f};
        break;
    case BlockSoundKind::Fall:
        result = {type.fall, v * 0.5f, p * 0.75f};
        break;
    case BlockSoundKind::Step:
        result = {type.step, v * 0.15f, p};
        break;
    }
    if (result.event.empty() || result.event == "intentionally_empty") return std::nullopt;
    return result;
}

std::filesystem::path BlockSounds::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("block_sounds-{}.txt", protocol);
}

const BlockSounds& BlockSounds::installed() {
    static const BlockSounds table = [] {
        BlockSounds loaded;
        const auto path = default_path(protocol::kProtocol_1_21_11);
        if (!loaded.load(path)) {
            std::cerr << std::format("no block sounds at {}; every block sounds silent until "
                                     "tools/bake_all.py bakes them\n", path.string());
        }
        return loaded;
    }();
    return table;
}

bool BlockSounds::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    std::stringstream text;
    text << input.rdbuf();
    return parse(text.str());
}

bool BlockSounds::parse(std::string_view text) {
    blocks_.clear();
    for (const auto line : split(text, '\n')) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split(line, '\t');
        if (fields.size() != 9) continue;
        BlockSoundType type;
        type.name = std::string(fields[1]);
        type.volume = to_float(fields[2]);
        type.pitch = to_float(fields[3]);
        type.break_sound = event_or_empty(fields[4]);
        type.step = event_or_empty(fields[5]);
        type.place = event_or_empty(fields[6]);
        type.hit = event_or_empty(fields[7]);
        type.fall = event_or_empty(fields[8]);
        if (fields[0] == "minecraft:stone") stone_ = type;
        blocks_.insert_or_assign(std::string(bare_name(fields[0])), std::move(type));
    }
    return !blocks_.empty();
}

const BlockSoundType& BlockSounds::find(std::string_view block) const {
    const auto found = blocks_.find(bare_name(block));
    return found == blocks_.end() ? stone_ : found->second;
}

bool BlockSounds::contains(std::string_view block) const {
    return blocks_.contains(bare_name(block));
}

} // namespace mcpp::audio
