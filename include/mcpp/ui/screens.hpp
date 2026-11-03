#pragma once
#include "mcpp/ui/screen.hpp"

#include <memory>
#include <string_view>
#include <vector>

// Every screen by the name `plasma-client screenshot` and the client know it.
namespace mcpp::ui {

std::shared_ptr<Screen> make_screen(std::string_view name);
// The names make_screen knows, and whether each one belongs over a world.
struct ScreenName {
    std::string_view name;
    bool in_world;
};
const std::vector<ScreenName>& screen_names();

} // namespace mcpp::ui
