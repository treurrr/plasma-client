#include "audio/sounds.hpp"

#include "mcpp/json.hpp"

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

int32_t SoundIndex::total_weight(std::string_view event) const {
    const auto* definition = find(event);
    if (definition == nullptr) return 0;
    int32_t total = 0;
    for (const auto& entry : definition->entries) total += entry.weight;
    return total;
}

std::optional<ResolvedSound> SoundIndex::pick(std::string_view event,
                                              LegacyRandom& random) const {
    return pick(event, random, 0);
}

std::optional<ResolvedSound> SoundIndex::pick(std::string_view event, LegacyRandom& random,
                                              int depth) const {
    const auto* definition = find(event);
    const int32_t total = total_weight(event);
    if (definition == nullptr || total <= 0 || depth > kMaxDepth) return std::nullopt;
    int32_t left = random.next_int(total);
    const SoundEntry* chosen = &definition->entries.back();
    for (const auto& entry : definition->entries) {
        left -= entry.weight;
        if (left < 0) {
            chosen = &entry;
            break;
        }
    }
    ResolvedSound sound;
    if (chosen->is_event) {
        auto inner = pick(chosen->name, random, depth + 1);
        if (!inner) return std::nullopt;
        sound = std::move(*inner);
    } else {
        sound.file = file_name(chosen->name);
        sound.stream = chosen->stream;
        sound.attenuation_distance = chosen->attenuation_distance;
        sound.preload = chosen->preload;
    }
    sound.event = std::string(bare_name(event));
    sound.volume *= chosen->volume;
    sound.pitch *= chosen->pitch;
    return sound;
}

} // namespace mcpp::audio
