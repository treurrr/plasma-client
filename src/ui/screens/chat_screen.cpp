#include "mcpp/ui/chat_screen.hpp"

#include "mcpp/options.hpp"

#include <algorithm>
#include <cmath>

namespace mcpp::ui {
namespace {

constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kSuggestionFill = 0xD0000000u;
constexpr uint32_t kSuggestionGrey = 0xFFAAAAAAu;
constexpr int kSuggestionLineLimit = 10;
constexpr double kMouseScrollSpeed = 7.0;

// `StringUtil.trimChatMessage`: the game's own length cap.
constexpr size_t kMaxChatLength = 256;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// `CommandSuggestions.getLastWordIndex`: the start of the word the caret is in.
size_t last_word_index(const std::string& line) {
    if (line.empty()) return 0;
    size_t at = line.size();
    while (at > 0 && !is_space(line[at - 1])) --at;
    return at;
}

} // namespace

// `CommandSuggestions.updateCommandInfo`: the literal children of the node the
// written words lead to, those that start with the word being completed.
std::vector<Suggestion> command_suggestions(const proto::CommandTree& tree,
                                            const std::string& line, size_t cursor) {
    std::vector<Suggestion> out;
    if (line.empty() || line.front() != '/') return out;
    const proto::CommandNode* node = tree.root_node();
    if (node == nullptr) return out;
    const std::string head = line.substr(1, cursor >= 1 ? cursor - 1 : 0);

    std::vector<std::string> words;
    std::vector<size_t> starts;
    size_t at = 0;
    while (at <= head.size()) {
        while (at < head.size() && is_space(head[at])) ++at;
        if (at >= head.size()) break;
        const size_t word_start = at;
        while (at < head.size() && !is_space(head[at])) ++at;
        words.push_back(head.substr(word_start, at - word_start));
        starts.push_back(word_start + 1);
    }
    // A line that ends in a space is completing a new, empty word.
    const bool new_word = head.empty() || is_space(head.back());
    const std::string partial = new_word ? std::string() : (words.empty() ? std::string() : words.back());
    const size_t partial_start = new_word ? cursor : (starts.empty() ? cursor : starts.back());
    const size_t consumed = new_word ? words.size() : (words.empty() ? 0 : words.size() - 1);

    const auto resolve = [&tree](const proto::CommandNode* from) {
        if (from != nullptr && from->redirect >= 0 &&
            static_cast<size_t>(from->redirect) < tree.nodes.size()) {
            return &tree.nodes[static_cast<size_t>(from->redirect)];
        }
        return from;
    };
    for (size_t i = 0; i < consumed; ++i) {
        const proto::CommandNode* next = nullptr;
        for (const int32_t child : node->children) {
            if (static_cast<size_t>(child) >= tree.nodes.size()) continue;
            const auto& candidate = tree.nodes[static_cast<size_t>(child)];
            if (candidate.kind == proto::CommandNode::Kind::Literal && candidate.name == words[i]) {
                next = &candidate;
                break;
            }
        }
        if (next == nullptr) {
            // An argument takes any word; the walk follows the first one.
            for (const int32_t child : node->children) {
                if (static_cast<size_t>(child) >= tree.nodes.size()) continue;
                const auto& candidate = tree.nodes[static_cast<size_t>(child)];
                if (candidate.kind == proto::CommandNode::Kind::Argument) {
                    next = &candidate;
                    break;
                }
            }
        }
        if (next == nullptr) return out;
        node = resolve(next);
    }
    for (const int32_t child : node->children) {
        if (static_cast<size_t>(child) >= tree.nodes.size()) continue;
        const auto& candidate = tree.nodes[static_cast<size_t>(child)];
        if (candidate.kind != proto::CommandNode::Kind::Literal) continue;
        if (!partial.empty() && candidate.name.compare(0, partial.size(), partial) != 0) continue;
        out.push_back({candidate.name, partial_start, cursor});
    }
    std::sort(out.begin(), out.end(),
              [](const Suggestion& a, const Suggestion& b) { return a.text < b.text; });
    return out;
}

ChatScreen::ChatScreen(std::string initial, bool draft)
    : Screen(translatable("chat_screen.title")), initial_(std::move(initial)), is_draft_(draft) {}

// `ChatScreen.init`: the input line four in from the left, twelve high, along the bottom of the screen, with no border.
void ChatScreen::init() {
    history_pos_ = chat != nullptr ? static_cast<int>(chat->recent.size()) : 0;
    auto& box = add<EditBox>(4, height - 12, width - 4, 12, translatable("chat.editBox"));
    input = &box;
    box.max_length = kMaxChatLength;
    box.bordered = false;
    box.set_value(initial_);
    if (is_draft_) {
        box.text_colour = 0xFFAAAAAAu;
    }
    box.on_change = [this](const std::string&) {
        show_suggestions = true;
        is_draft_ = false;
        if (input != nullptr) input->text_colour = 0xFFE0E0E0u;
        update_suggestions();
    };
    if (manager != nullptr) box.platform = &manager->platform;
    set_focus(input, false);
    update_suggestions();
}

// `ChatScreen.onClose`: the screen goes and nothing is sent.
void ChatScreen::on_close() {
    if (manager != nullptr) manager->close();
}

// `ChatScreen.removed`: the chat scroll goes back to the bottom.
void ChatScreen::removed() {
    if (chat != nullptr) chat->reset_scroll();
    if (input != nullptr) initial_ = input->value();
}

// `ChatScreen.normalizeChatMessage`: trimmed, runs of spaces squeezed to one,
// and cut to the length the game allows.
std::string ChatScreen::normalise(std::string line) {
    size_t a = 0;
    size_t b = line.size();
    while (a < b && static_cast<unsigned char>(line[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(line[b - 1]) <= ' ') --b;
    std::string trimmed = line.substr(a, b - a);
    std::string out;
    bool space = false;
    for (const char c : trimmed) {
        if (is_space(c)) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    if (out.size() > kMaxChatLength) out.resize(kMaxChatLength);
    return out;
}

// `ChatScreen.handleChatInput`: a line that starts with a slash is a command.
void ChatScreen::handle_chat_input(const std::string& line, bool remember) {
    const std::string message = normalise(line);
    if (message.empty()) return;
    if (remember && chat != nullptr) {
        if (chat->recent.empty() || chat->recent.back() != message) {
            chat->recent.push_back(message);
        }
    }
    if (manager == nullptr) return;
    if (message.front() == '/') {
        if (manager->host.command) manager->host.command(message.substr(1));
    } else if (manager->host.chat) {
        manager->host.chat(message);
    }
}

void ChatScreen::update_suggestions() {
    suggestions_.clear();
    chosen_ = 0;
    offset_ = 0;
    if (commands == nullptr || input == nullptr) return;
    suggestions_ = command_suggestions(*commands, input->value(), input->value().size());
}

// `CommandSuggestions.SuggestionsList.useSuggestion`: the word under the
// caret replaced by the one chosen.
void ChatScreen::apply_suggestion() {
    if (input == nullptr || suggestions_.empty()) return;
    const Suggestion& chosen = suggestions_[std::min(chosen_, suggestions_.size() - 1)];
    std::string line = input->value();
    const size_t start = std::min(chosen.start, line.size());
    const size_t end = std::min(std::max(chosen.end, start), line.size());
    line = line.substr(0, start) + chosen.text + line.substr(end);
    input->set_value(line);
}

// `ChatScreen.keyPressed`: the completion first, then Enter to send, then the
// arrows for the history and the page keys for the chat's own scroll.
bool ChatScreen::key_pressed(const KeyEvent& event) {
    if (!suggestions_.empty() && show_suggestions) {
        if (event.key == key::Up || event.key == key::Down) {
            const int step = event.key == key::Up ? -1 : 1;
            const auto count = static_cast<int>(suggestions_.size());
            const int next = (static_cast<int>(chosen_) + step + count) % count;
            chosen_ = static_cast<size_t>(next);
            if (chosen_ < offset_) offset_ = chosen_;
            if (chosen_ >= offset_ + kSuggestionLineLimit) {
                offset_ = chosen_ - kSuggestionLineLimit + 1;
            }
            return true;
        }
        if (event.key == key::Tab) {
            apply_suggestion();
            return true;
        }
    }
    if (event.key == key::Tab) {
        show_suggestions = true;
        update_suggestions();
        if (suggestions_.size() == 1) apply_suggestion();
        return true;
    }
    if (is_draft_ && event.key == key::Backspace) {
        if (input != nullptr) input->set_value("");
        is_draft_ = false;
        return true;
    }
    if (Screen::key_pressed(event)) return true;
    if (event.key == key::Enter || event.key == key::KeypadEnter) {
        if (input != nullptr) handle_chat_input(input->value(), true);
        if (manager != nullptr) manager->close();
        return true;
    }
    const auto* options = manager != nullptr ? manager->options : nullptr;
    switch (event.key) {
    case key::Down:
        move_in_history(1);
        return true;
    case key::Up:
        move_in_history(-1);
        return true;
    case key::PageUp:
        if (chat != nullptr && options != nullptr) {
            chat->scroll(chat->lines_per_page(*options, true) - 1, *options, true);
        }
        return true;
    case key::PageDown:
        if (chat != nullptr && options != nullptr) {
            chat->scroll(-chat->lines_per_page(*options, true) + 1, *options, true);
        }
        return true;
    default:
        return false;
    }
}

// `ChatScreen.moveInHistory`: the line one step back or forward in what was
// sent, with what was typed kept at the end.
void ChatScreen::move_in_history(int by) {
    if (chat == nullptr || input == nullptr) return;
    const int count = static_cast<int>(chat->recent.size());
    int next = std::clamp(history_pos_ + by, 0, count);
    if (next == history_pos_) return;
    if (next == count) {
        history_pos_ = count;
        input->set_value(history_buffer_);
        return;
    }
    if (history_pos_ == count) history_buffer_ = input->value();
    input->set_value(chat->recent[static_cast<size_t>(next)]);
    show_suggestions = false;
    history_pos_ = next;
}

// `ChatScreen.mouseScrolled`: seven lines a notch, one with shift held.
bool ChatScreen::mouse_scrolled(double, double, double, double dy) {
    double amount = std::clamp(dy, -1.0, 1.0);
    if (manager == nullptr || manager->options == nullptr || chat == nullptr) return true;
    if (!manager->shift_down) amount *= kMouseScrollSpeed;
    chat->scroll(static_cast<int>(amount), *manager->options, true);
    return true;
}

// `ChatScreen.mouseClicked`: a click on a line with a click event follows it,
// or with shift held puts its insertion in the box.
bool ChatScreen::mouse_clicked(double mx, double my, int button) {
    if (!suggestions_.empty() && show_suggestions) {
        const int lines = static_cast<int>(std::min<size_t>(suggestions_.size() - offset_,
                                                            kSuggestionLineLimit));
        const int top = height - 12 - 3 - lines * 12;
        if (my >= top && my < top + lines * 12) {
            chosen_ = offset_ + static_cast<size_t>((static_cast<int>(my) - top) / 12);
            apply_suggestion();
            return true;
        }
    }
    if (button == 0 && last_gfx_ != nullptr) {
        if (const Style* style = style_at(*last_gfx_, mx, my)) {
            const bool insertion = manager != nullptr && manager->shift_down;
            if (insertion && !style->insertion.empty()) {
                if (input != nullptr) input->insert(style->insertion);
                return true;
            }
            if (!insertion && style->click_action == "suggest_command") {
                if (input != nullptr) input->set_value(style->click_value);
                return true;
            }
            if (!insertion && style->click_action == "run_command") {
                handle_chat_input(style->click_value, false);
                if (manager != nullptr) manager->close();
                return true;
            }
            if (!insertion && style->click_action == "copy_to_clipboard") {
                if (manager != nullptr && manager->platform.set_clipboard) {
                    manager->platform.set_clipboard(style->click_value);
                }
                return true;
            }
        }
    }
    return Screen::mouse_clicked(mx, my, button);
}

// `ChatComponent.render`: the same arithmetic, read backwards, to find which
// line and which run the pointer rests on.
const Style* ChatScreen::style_at(const GuiGraphics& gfx, double mx, double my) const {
    if (chat == nullptr || manager == nullptr || manager->options == nullptr) return nullptr;
    const Options& options = *manager->options;
    if (options.get_int("chatVisibility") == 2) return nullptr;
    const auto& lines = chat->lines();
    if (lines.empty()) return nullptr;
    const auto scale = static_cast<float>(options.get_double("chatScale"));
    if (scale <= 0.0f) return nullptr;
    const double spacing = options.get_double("chatLineSpacing");
    const int height_per_line = static_cast<int>(9.0 * (spacing + 1.0));
    const int bottom = static_cast<int>(std::floor(static_cast<float>(gfx.height - 40) / scale));
    const int width = static_cast<int>(
        std::ceil(hud::chat_width(options.get_double("chatWidth")) / static_cast<double>(scale)));
    const double chat_x = mx / static_cast<double>(scale) - 4.0;
    const double chat_y = my / static_cast<double>(scale);
    if (chat_x < 0.0 || chat_x > static_cast<double>(width)) return nullptr;
    const int page = chat->lines_per_page(options, true);
    const int scroll = chat->scroll_amount();
    const int visible =
        std::min(static_cast<int>(lines.size()) - scroll, page);
    for (int i = 0; i < visible; ++i) {
        const int line_bottom = bottom - i * height_per_line;
        if (chat_y < line_bottom - height_per_line || chat_y >= line_bottom) continue;
        const auto& line = lines[static_cast<size_t>(i + scroll)];
        double pen = 0.0;
        for (const auto& run : line.runs) {
            const double run_width = gfx.font.width(std::vector<Run>{run});
            if (chat_x >= pen && chat_x < pen + run_width) return &run.style;
            pen += run_width;
        }
        return nullptr;
    }
    return nullptr;
}

// `ChatScreen.render`: the bar behind the input line, the chat over it, the
// widgets, then the completions.
void ChatScreen::render(GuiGraphics& gfx, double mx, double my) {
    last_gfx_ = &gfx;
    const Options* options = manager != nullptr ? manager->options : nullptr;
    // `Options.getBackgroundColor`: black at the text background's opacity.
    uint32_t background = 0x80000000u;
    if (options != nullptr) {
        const double opacity = options->get_bool("backgroundForChatOnly")
                                   ? 1.0
                                   : options->get_double("textBackgroundOpacity");
        background = static_cast<uint32_t>(std::floor(opacity * 255.0)) << 24;
    }
    gfx.fill(2, height - 14, width - 2, height - 2, background);
    if (chat != nullptr && options != nullptr) {
        chat->render(gfx, *options, gui_ticks, true, static_cast<int>(mx), static_cast<int>(my));
    }
    Screen::render(gfx, mx, my);
    render_suggestions(gfx, mx, my);
    if (const Style* style = style_at(gfx, mx, my)) {
        if (style->hover_action == "show_text" && style->hover_text != nullptr) {
            gfx.set_tooltip_for_next_frame(*style->hover_text, static_cast<int>(mx),
                                           static_cast<int>(my));
        }
    }
}

// `CommandSuggestions.SuggestionsList.render`: the box above the input line,
// the chosen line in yellow and the rest in grey.
void ChatScreen::render_suggestions(GuiGraphics& gfx, double mx, double my) {
    if (suggestions_.empty() || !show_suggestions) return;
    int widest = 0;
    for (const auto& suggestion : suggestions_) {
        widest = std::max(widest, gfx.width_of(suggestion.text));
    }
    const size_t start = std::min(suggestions_[0].start, static_cast<size_t>(1024));
    const std::string before = input != nullptr ? input->value().substr(0, start) : std::string();
    const int anchor = std::clamp(4 + gfx.width_of(before), 0, 4 + (width - 4) - 8 - widest);
    const int lines =
        static_cast<int>(std::min<size_t>(suggestions_.size() - offset_, kSuggestionLineLimit));
    const int box_x = anchor - 1;
    const int box_y = height - 12 - 3 - lines * 12;
    const int box_w = widest + 1;
    for (int i = 0; i < lines; ++i) {
        const int line_y = box_y + 12 * i;
        gfx.fill(box_x, line_y, box_x + box_w, line_y + 12, kSuggestionFill);
        const size_t at = offset_ + static_cast<size_t>(i);
        const bool hovered = mx > box_x && mx < box_x + box_w && my > line_y && my < line_y + 12;
        const uint32_t colour = at == chosen_ || hovered ? 0xFFFFFF00u : kSuggestionGrey;
        gfx.draw_string(suggestions_[at].text, box_x + 1, line_y + 2, colour);
    }
}

DeathScreen::DeathScreen(std::optional<Component> cause, bool hardcore_world, int score)
    : Screen(translatable(hardcore_world ? "deathScreen.title.hardcore" : "deathScreen.title")),
      cause_of_death(std::move(cause)), hardcore(hardcore_world) {
    Style yellow;
    yellow.colour = 0xFFFF55u;
    death_score =
        translatable("deathScreen.score.value", {literal(std::to_string(score), yellow)});
}

// `DeathScreen.init`: Respawn a quarter down plus 72 and Title Screen plus 96, both 200 wide and both dead for the first second.
void DeathScreen::init() {
    delay_ = 0;
    exit_buttons_.clear();
    exit_buttons_.push_back(&add<Button>(
        width / 2 - 100, height / 4 + 72, 200, 20,
        translatable(hardcore ? "deathScreen.spectate" : "deathScreen.respawn"),
        [this](Button& button) {
            button.active = false;
            if (manager != nullptr && manager->host.respawn) manager->host.respawn();
            if (manager != nullptr) manager->close();
        }));
    exit_buttons_.push_back(&add<Button>(width / 2 - 100, height / 4 + 96, 200, 20,
                                         translatable("deathScreen.titleScreen"),
                                         [this](Button&) {
                                             if (manager != nullptr &&
                                                 manager->host.disconnect) {
                                                 manager->host.disconnect();
                                             }
                                         }));
    set_buttons_active(false);
}

// `DeathScreen.setButtonsActive`: both ways out at once.
void DeathScreen::set_buttons_active(bool on) {
    for (Button* button : exit_buttons_) button->active = on;
}

// `DeathScreen.tick`: the buttons wake after twenty ticks.
void DeathScreen::tick() {
    Screen::tick();
    ++delay_;
    if (delay_ == 20) set_buttons_active(true);
}

// `DeathScreen.renderDeathBackground`: the red gradient over the world.
void DeathScreen::render_background(GuiGraphics& gfx, double, double) {
    gfx.fill_gradient(0, 0, width, height, 0x60500000u, 0xA0803030u);
}

// `DeathScreen.visitText`: the title at twice the size thirty down, what
// killed the player at 85 and the score at 100.
void DeathScreen::render(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    const int middle = width / 2;
    gfx.push_pose();
    gfx.scale_by(2.0f, 2.0f);
    gfx.draw_centered_string(title, middle / 2, 30, kWhite);
    gfx.pop_pose();
    if (cause_of_death) gfx.draw_centered_string(*cause_of_death, middle, 85, kWhite);
    gfx.draw_centered_string(death_score, middle, 100, kWhite);
}

} // namespace mcpp::ui
