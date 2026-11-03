#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace mcpp::audio {

// A block's sound type: five events and the volume and pitch they are played
// at. Which type a block has is in no report; tools/gen_block_sounds.py asks
// the game and bake_all writes it as block_sounds-<protocol>.txt in the data
// directory, one row per block.
struct BlockSoundType {
    std::string name;
    float volume = 1.0f;
    float pitch = 1.0f;
    std::string break_sound;
    std::string step;
    std::string place;
    std::string hit;
    std::string fall;
};

enum class BlockSoundKind { Break, Step, Place, Hit, Fall };

struct BlockSoundCue {
    std::string event;
    float volume = 1.0f;
    float pitch = 1.0f;
};

// The volume and pitch each kind is played at, from the type's own pair: a
// break or a place at half of one more than the volume and four fifths of the
// pitch, the cracking while digging at an eighth and half, a fall at half and
// three quarters, a step at 0.15 of the volume and the pitch as it is.
std::optional<BlockSoundCue> cue(const BlockSoundType& type, BlockSoundKind kind);

class BlockSounds {
public:
    static std::filesystem::path default_path(int32_t protocol);
    // The baked table for 1.21.11, read once; empty, after saying so on
    // stderr, when it was never baked.
    static const BlockSounds& installed();

    bool load(const std::filesystem::path& path);

    // Rows are tab separated: block, type, volume, pitch, break, step, place,
    // hit, fall. intentionally_empty is an event the type does not have.
    bool parse(std::string_view text);
    size_t size() const { return blocks_.size(); }
    bool empty() const { return blocks_.empty(); }
    // By registry name, with or without the minecraft: namespace. A block the
    // table does not name sounds like stone, which is the game's default.
    const BlockSoundType& find(std::string_view block) const;
    bool contains(std::string_view block) const;

private:
    std::map<std::string, BlockSoundType, std::less<>> blocks_;
    BlockSoundType stone_;
};

} // namespace mcpp::audio
