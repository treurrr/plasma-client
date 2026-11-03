#pragma once
#include "mcpp/options.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The game's 59 key bindings, by the names options.txt stores them under,
// with the game's defaults, rebindable to any key or mouse button.
namespace mcpp {

struct InputKey {
    enum class Type : uint8_t { Keyboard, Mouse };
    Type type = Type::Keyboard;
    int code = -1;   // GLFW key code or mouse button; -1 is unbound

    bool unbound() const { return code < 0; }
    bool operator==(const InputKey&) const = default;

    // "key.keyboard.left.shift", "key.mouse.middle".
    std::string name() const;
    static std::optional<InputKey> from_name(std::string_view name);
    static InputKey keyboard(int code) { return {Type::Keyboard, code}; }
    static InputKey mouse(int button) { return {Type::Mouse, button}; }
};

struct Keybind {
    std::string name;       // "key.jump"
    std::string category;   // "key.category.minecraft.movement"
    InputKey fallback;
    InputKey current;
    // Held this frame, and presses not yet taken.
    bool down = false;
    int clicks = 0;

    bool is_default() const { return current == fallback; }
};

class Keybinds {
public:
    Keybinds();

    std::vector<Keybind>& all() { return binds_; }
    const std::vector<Keybind>& all() const { return binds_; }
    Keybind* find(std::string_view name);
    const Keybind* find(std::string_view name) const;

    void load(const Options& options);
    void store(Options& options) const;

    // Raw input, fanned out to every binding on that key.
    void press(InputKey key);
    void release(InputKey key);
    void release_all();
    bool down(std::string_view name) const;
    // A press since last asked, taken one at a time.
    bool consume(std::string_view name);
    int take_clicks(std::string_view name);

    // Bindings other than this one on the same key, for the clash warning.
    std::vector<const Keybind*> clashes(const Keybind& bind) const;

    // The categories in the order the controls screen lists them.
    static const std::vector<std::string>& categories();

private:
    std::vector<Keybind> binds_;
};

} // namespace mcpp
