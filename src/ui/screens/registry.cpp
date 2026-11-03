#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/screens.hpp"

namespace mcpp::ui {

const std::vector<ScreenName>& screen_names() {
    static const std::vector<ScreenName> names = {
        {"inventory", true},
    };
    return names;
}

std::shared_ptr<Screen> make_screen(std::string_view name) {
    if (name == "inventory") return std::make_shared<InventoryScreen>();
    return nullptr;
}

} // namespace mcpp::ui
