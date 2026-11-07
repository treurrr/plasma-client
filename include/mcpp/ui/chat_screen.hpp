#pragma once
#include "mcpp/proto/commands.hpp"
#include "mcpp/ui/hud/hud.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/widgets.hpp"

#include <optional>
#include <string>
#include <vector>

// The chat line the player types in, with its history, the completion from the
// server's own command tree and the click and hover events of the lines above
// it; and the screen that comes up when the player dies.
namespace mcpp::ui {

// `CommandSuggestions`: what the server's command tree offers for the word the
// caret is in.
struct Suggestion {
    std::string text;
    // Where in the line the word being completed starts and ends.
    size_t start = 0;
    size_t end = 0;
};

std::vector<Suggestion> command_suggestions(const proto::CommandTree& tree,
                                            const std::string& line, size_t cursor);

class ChatScreen : public Screen {
public:
    ChatScreen(std::string initial, bool draft);

    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    void render_background(GuiGraphics&, double, double) override {}
    bool is_pause_screen() const override { return false; }
    bool is_in_game_ui() const override { return true; }
    void on_close() override;
    void removed() override;
    bool key_pressed(const KeyEvent& event) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool mouse_scrolled(double mx, double my, double dx, double dy) override;

    void move_in_history(int by);
    // `ChatScreen.handleChatInput`, `normalizeChatMessage`: the line trimmed
    // and its runs of spaces squeezed, then sent as chat or as a command.
    void handle_chat_input(const std::string& line, bool remember);
    static std::string normalise(std::string line);

    EditBox* input = nullptr;
    // What the screen draws over and talks to: the hud's chat, the options and
    // the command tree, all set by the client before the screen is opened.
    hud::Chat* chat = nullptr;
    const proto::CommandTree* commands = nullptr;
    int32_t gui_ticks = 0;

private:
    void update_suggestions();
    void apply_suggestion();
    void render_suggestions(GuiGraphics& gfx, double mx, double my);
    const Style* style_at(const GuiGraphics& gfx, double mx, double my) const;

    std::string initial_;
    bool is_draft_ = false;
    std::string history_buffer_;
    int history_pos_ = -1;
    GuiGraphics* last_gfx_ = nullptr;
    std::vector<Suggestion> suggestions_;
    size_t chosen_ = 0;
    size_t offset_ = 0;
    bool show_suggestions_ = false;
};

// `DeathScreen`: You Died, what killed the player, the score, and the two
// ways out.
class DeathScreen : public Screen {
public:
    DeathScreen(std::optional<Component> cause, bool hardcore_world, int score);

    void init() override;
    void tick() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    void render_background(GuiGraphics& gfx, double mx, double my) override;
    bool should_close_on_esc() const override { return false; }
    bool is_pause_screen() const override { return false; }

    std::optional<Component> cause_of_death;
    bool hardcore = false;
    Component death_score;

private:
    void set_buttons_active(bool on);

    int delay_ = 0;
    std::vector<Button*> exit_buttons_;
};

} // namespace mcpp::ui
