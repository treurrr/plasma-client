#include "mcpp/ui/pause_screen.hpp"

namespace mcpp::ui {
namespace {

constexpr int kButtonWidthFull = 204;
constexpr int kButtonWidthHalf = 98;
constexpr int kMenuPaddingTop = 50;

// `PauseScreen.openScreenButton`, `PauseScreen.openLinkButton`: a half-width button; the screens and links behind these are not in this client yet.
Button& half_button(Screen& screen, const char* key) {
    return screen.add<Button>(0, 0, kButtonWidthHalf, 20, translatable(key), [](Button&) {});
}

// `PauseScreen.addFeedbackButtons`: Give Feedback and Report Bugs, the second live outside the side series.
void add_feedback_buttons(Screen& screen, GridLayout::RowHelper& row) {
    row.add_child(half_button(screen, "menu.sendFeedback"));
    row.add_child(half_button(screen, "menu.reportBugs")).active = true;
}

int title_width(const Screen& screen) {
    const auto* manager = screen.manager;
    return manager != nullptr && manager->font != nullptr ? manager->font->width(screen.title) : 0;
}

} // namespace

PauseScreen::PauseScreen(bool show, bool links)
    : Screen(translatable(show ? "menu.game" : "menu.paused")), show_pause_menu(show),
      server_links(links) {}

// `PauseScreen.init`: the menu, then its title 40 down (10 without the menu).
void PauseScreen::init() {
    if (show_pause_menu) create_pause_menu();
    const int w = title_width(*this);
    add<StringWidget>(width / 2 - w / 2, show_pause_menu ? 40 : 10, w, 9, title);
}

// `PauseScreen.createPauseMenu`: a two-column grid, put a quarter of the way down the screen.
void PauseScreen::create_pause_menu() {
    auto& grid = layout<GridLayout>();
    grid.default_cell_setting().padding(4, 4, 4, 0);
    auto row = grid.create_row_helper(2);
    ScreenManager* owner = manager;

    LayoutSettings first = grid.new_cell_settings();
    first.padding_top = kMenuPaddingTop;
    row.add_child(add<Button>(0, 0, kButtonWidthFull, 20, translatable("menu.returnToGame"),
                              [owner](Button&) {
                                  if (owner != nullptr) owner->close();
                              }),
                  2, first);
    row.add_child(half_button(*this, "gui.advancements"));
    row.add_child(half_button(*this, "gui.stats"));
    if (!server_links) {
        add_feedback_buttons(*this, row);
    } else {
        // `PauseScreen.addFeedbackSubscreenAndCustomDialogButtons`: Feedback... and the server's links.
        row.add_child(add<Button>(0, 0, kButtonWidthHalf, 20, translatable("menu.feedback"),
                                  [owner](Button&) {
                                      if (owner == nullptr) return;
                                      owner->set_screen(std::make_shared<FeedbackSubScreen>(
                                          owner->current_shared()));
                                  }));
        auto& links = row.add_child(half_button(*this, "menu.server_links"));
        links.tooltip = {translatable("menu.custom_options.tooltip")};
    }
    row.add_child(half_button(*this, "menu.options"));
    row.add_child(half_button(*this, "menu.playerReporting"));
    disconnect_button = &row.add_child(
        add<Button>(0, 0, kButtonWidthFull, 20, translatable("menu.disconnect"),
                    [owner](Button& button) {
                        button.active = false;
                        if (owner != nullptr && owner->host.disconnect) owner->host.disconnect();
                    }),
        2);
    grid.arrange_elements();
    FrameLayout::align_in_rectangle(grid, 0, 0, width, height, 0.5f, 0.25f);
}

// `PauseScreen.renderBackground`: the blurred, darkened world only behind the full menu.
void PauseScreen::render_background(GuiGraphics& gfx, double mx, double my) {
    if (show_pause_menu) Screen::render_background(gfx, mx, my);
}

FeedbackSubScreen::FeedbackSubScreen(std::shared_ptr<Screen> back)
    : Screen(translatable("menu.feedback.title")) {
    parent = std::move(back);
}

// `PauseScreen.FeedbackSubScreen.init`: the title in the header, the two buttons, Back in the footer.
void FeedbackSubScreen::init() {
    auto& frame = layout<HeaderAndFooterLayout>(width, height);
    const int w = title_width(*this);
    frame.add_to_header(add<StringWidget>(0, 0, w, 9, title));
    auto& grid = layout<GridLayout>();
    frame.add_to_contents(grid);
    grid.default_cell_setting().padding(4, 4, 4, 0);
    auto row = grid.create_row_helper(2);
    add_feedback_buttons(*this, row);
    frame.add_to_footer(
        add<Button>(0, 0, 200, 20, translatable("gui.back"), [this](Button&) { on_close(); }));
    frame.arrange_elements();
}

} // namespace mcpp::ui
