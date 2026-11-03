#include "audio/sounds.hpp"

#include "mcpp/json.hpp"

#include <algorithm>
#include <format>

namespace mcpp::audio {
namespace {

constexpr int kMaxDepth = 8;

std::string file_name(std::string_view name) {
    std::string_view space = "minecraft";
    if (const auto colon = name.find(':'); colon != std::string_view::npos) {
        space = name.substr(0, colon);
        name = name.substr(colon + 1);
    }
    return std::format("{}/sounds/{}.ogg", space, name);
}

} // namespace

std::string_view bare_name(std::string_view name) {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name;
}

void SoundIndex::parse(std::string_view text) {
    const auto root = json::parse(text);
    const auto* events = root.as_object();
    if (events == nullptr) throw ProtocolError("sounds.json is not an object");
    events_.clear();
    for (const auto& [name, body] : *events) {
        SoundEventDefinition definition;
        definition.subtitle = body.find("subtitle") ? body.find("subtitle")->string_or("") : "";
        if (const auto* sounds = body.find("sounds"); sounds && sounds->as_array()) {
            for (const auto& item : *sounds->as_array()) {
                SoundEntry entry;
                if (const auto* plain = item.as_string()) {
                    entry.name = *plain;
                } else if (item.as_object() != nullptr) {
                    const auto number = [&](std::string_view key, double fallback) {
                        const auto* value = item.find(key);
                        return value ? value->number_or(fallback) : fallback;
                    };
                    const auto flag = [&](std::string_view key) {
                        const auto* value = item.find(key);
                        return value && value->as_bool().value_or(false);
                    };
                    entry.name = item.find("name") ? item.find("name")->string_or("") : "";
                    entry.is_event = item.find("type") &&
                                     item.find("type")->string_or("file") == "event";
                    entry.volume = static_cast<float>(number("volume", 1.0));
                    entry.pitch = static_cast<float>(number("pitch", 1.0));
                    entry.weight = static_cast<int32_t>(number("weight", 1.0));
                    entry.stream = flag("stream");
                    entry.preload = flag("preload");
                    entry.attenuation_distance =
                        static_cast<int32_t>(number("attenuation_distance", 16.0));
                } else {
                    continue;
                }
                if (!entry.name.empty()) definition.entries.push_back(std::move(entry));
            }
        }
        events_.insert_or_assign(std::string(bare_name(name)), std::move(definition));
    }
}

const SoundEventDefinition* SoundIndex::find(std::string_view event) const {
    const auto found = events_.find(bare_name(event));
    return found == events_.end() ? nullptr : &found->second;
}

void SoundIndex::prune(const std::function<bool(std::string_view)>& exists) {
    for (auto& [name, definition] : events_) {
        std::erase_if(definition.entries, [&](const SoundEntry& entry) {
            return !entry.is_event && !exists(file_name(entry.name));
        });
    }
}

std::vector<std::string> SoundIndex::preload_files() const {
    std::vector<std::string> files;
    for (const auto& [name, definition] : events_) {
        for (const auto& entry : definition.entries) {
            if (entry.preload && !entry.is_event) files.push_back(file_name(entry.name));
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

int32_t SoundIndex::total_weight(std::string_view event) const {
    return total_weight(find(event), 0);
}

// `WeighedSoundEvents.getWeight`: the entries' weights summed.
int32_t SoundIndex::total_weight(const SoundEventDefinition* definition, int depth) const {
    if (definition == nullptr || depth > kMaxDepth) return 0;
    int32_t total = 0;
    for (const auto& entry : definition->entries) total += weight_of(entry, depth);
    return total;
}

// A reference weighs what the whole event it names weighs, not its own weight.
int32_t SoundIndex::weight_of(const SoundEntry& entry, int depth) const {
    return entry.is_event ? total_weight(find(entry.name), depth + 1) : entry.weight;
}

std::optional<ResolvedSound> SoundIndex::pick(std::string_view event,
                                              jmath::LegacyRandom& random) const {
    auto sound = pick(find(event), random, 0);
    if (sound) sound->event = std::string(bare_name(event));
    return sound;
}

// `WeighedSoundEvents.getSound`: one weighted draw, an event reference multiplying in its volume and pitch.
std::optional<ResolvedSound> SoundIndex::pick(const SoundEventDefinition* definition,
                                              jmath::LegacyRandom& random, int depth) const {
    const int32_t total = total_weight(definition, depth);
    if (definition == nullptr || definition->entries.empty() || total == 0) return std::nullopt;
    int32_t left = random.next_int(total);
    for (const auto& entry : definition->entries) {
        left -= weight_of(entry, depth);
        if (left >= 0) continue;
        if (!entry.is_event) {
            ResolvedSound sound;
            sound.file = file_name(entry.name);
            sound.volume = entry.volume;
            sound.pitch = entry.pitch;
            sound.stream = entry.stream;
            sound.attenuation_distance = entry.attenuation_distance;
            sound.preload = entry.preload;
            return sound;
        }
        auto sound = pick(find(entry.name), random, depth + 1);
        if (!sound) return std::nullopt;
        sound->volume *= entry.volume;
        sound->pitch *= entry.pitch;
        sound->stream = sound->stream || entry.stream;
        return sound;
    }
    return std::nullopt;
}

} // namespace mcpp::audio
