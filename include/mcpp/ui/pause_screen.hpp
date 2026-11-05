#pragma once
#include "mcpp/ui/widgets.hpp"

#include <memory>

namespace mcpp::ui {

// What Escape opens in a world. Buttons for screens this client does not
// have yet keep their place in the grid and open nothing.
class PauseScreen : public Screen {
public:
    explicit PauseScreen(bool show_pause_menu, bool server_links = false);
    void init() override;
    void render_background(GuiGraphics& gfx, double mx, double my) override;

    bool show_pause_menu;
    // The server sent links: the feedback pair folds into one button and the
    // links take the other.
    bool server_links;
    Button* disconnect_button = nullptr;

private:
    void create_pause_menu();
};

// The two feedback buttons on a screen of their own, when the server's links
// took their place in the pause menu.
class FeedbackSubScreen : public Screen {
public:
    explicit FeedbackSubScreen(std::shared_ptr<Screen> back);
    void init() override;
};

} // namespace mcpp::ui
