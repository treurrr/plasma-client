#include "audio/level_events.hpp"

namespace mcpp::audio {

// Volumes and pitches as the Minecraft Wiki's sound tables give them for each
// event (the Anvil page for 1029-1031).
std::optional<LevelEventSound> level_event_sound(int32_t type) {
    switch (type) {
    case 1029: return LevelEventSound{"block.anvil.destroy", Category::Block, 1.0f, 0.9f, 1.0f};
    case 1030: return LevelEventSound{"block.anvil.use", Category::Block, 1.0f, 0.9f, 1.0f};
    case 1031: return LevelEventSound{"block.anvil.land", Category::Block, 0.3f, 0.9f, 1.0f};
    default: return std::nullopt;
    }
}

} // namespace mcpp::audio
