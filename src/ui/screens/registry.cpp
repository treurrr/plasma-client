#include "mcpp/protocol_versions.hpp"
#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/pause_screen.hpp"
#include "mcpp/ui/screens.hpp"
#include "mcpp/ui/title_screen.hpp"

namespace mcpp::ui {

const CreativeTabs& creative_tabs() {
    static const CreativeTabs tabs = [] {
        ItemTable items;
        items.load(ItemTable::default_path(protocol::kTargetProtocol));
        CreativeTabs out;
        out.load(CreativeTabs::default_path(protocol::kTargetProtocol), items);
        return out;
    }();
    return tabs;
}

const std::vector<ScreenName>& screen_names() {
    static const std::vector<ScreenName> names = {
        {"inventory", true},
        {"creative", true},
        {"title", false},
        {"pause", true},
    };
    return names;
}

std::shared_ptr<Screen> make_screen(std::string_view name) {
    if (name == "inventory") return std::make_shared<InventoryScreen>();
    if (name == "creative") return std::make_shared<CreativeModeInventoryScreen>(&creative_tabs(), false);
    if (name == "title") return std::make_shared<TitleScreen>();
    if (name == "pause") return std::make_shared<PauseScreen>(true);
    return nullptr;
}

} // namespace mcpp::ui
