#pragma once
#include "mcpp/keybinds.hpp"
#include "mcpp/options.hpp"
#include "mcpp/ui/entry_list.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/widgets.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The options screens, built from the game's own OptionInstance definitions:
// each option's name, tooltip, value set and label come from the table here,
// and the value itself lives in options.txt where vanilla keeps it.
namespace mcpp::ui {

// `Options.genericValueLabel`, `percentValueLabel`, `pixelValueLabel`.
Component generic_value_label(const Component& caption, Component value);
Component generic_value_label(const Component& caption, int value);
Component generic_value_or_off_label(const Component& caption, int value);
Component percent_value_label(const Component& caption, double value);
Component percent_value_or_off_label(const Component& caption, double value);
Component pixel_value_label(const Component& caption, int value);
// `CommonComponents.optionStatus`: "Name: ON" composed the game's way.
Component option_status(const Component& caption, bool on);
// `ChatComponent.getWidth`, `getHeight`: the chat box in gui pixels.
int chat_width_px(double fraction);
int chat_height_px(double fraction);

// `OptionInstance`: one option, as a cycle of values or a slider over them.
class OptionDef {
public:
    enum class Kind : uint8_t { Cycle, Slider };

    std::string key;   // the options.txt key, which is also this option's name
    Component caption;
    Kind kind = Kind::Cycle;
    // The value is written quoted, the way a string codec writes it.
    bool quoted = false;
    // Cycle: every value in the game's own order, as options.txt holds it.
    std::vector<std::string> values;
    // What the widget says, and what rests under the pointer.
    std::function<Component(const Component&, const std::string&)> label;
    std::function<std::vector<Component>(const std::string&)> tooltip;
    // Slider: the value as a position from 0 to 1, and back to a value.
    std::function<double(const std::string&)> to_slider;
    std::function<std::string(double)> from_slider;
    // Slider: the value one step along, where the option has steps.
    std::function<std::optional<std::string>(const std::string&, int)> step;
    // `SliderableValueSet.applyValueImmediately`: false waits 600 ms.
    bool apply_immediately = true;

    std::string get(const Options& options) const;
    void set(Options& options, const std::string& value) const;
    Component message(const Options& options) const;
    std::unique_ptr<Widget> create_button(Options& options, int x, int y, int width) const;
};

// Every option the screens reach, by its options.txt key.
const std::vector<OptionDef>& option_defs();
const OptionDef* option_def(std::string_view key);

// The game's OptionInstanceSliderButton: the slider that writes the option,
// at once or after the game's 600 ms.
class OptionSlider : public Slider {
public:
    OptionSlider(const OptionDef& def, Options& options, int x, int y, int w, int h);
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_released(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;
    void apply_unsaved_value();
    void reset_value();

    const OptionDef* def = nullptr;
    Options* options = nullptr;

private:
    std::optional<int64_t> apply_at_;
    int64_t last_millis_ = 0;
};

// `OptionsList`: rows 25 high, one big widget across or two side by side, and
// headers that take a line and a half.
class OptionsList : public EntryList {
public:
    OptionsList(Screen& owner, int list_w, int list_h, int top);
    int row_width() const override { return 310; }
    bool entries_can_be_selected() const override { return false; }

    void add_big(const OptionDef& def, Options& options);
    void add_small(const std::vector<const OptionDef*>& defs, Options& options);
    void add_small_widgets(std::vector<Widget*> widgets);
    void add_header(Component text);
    Widget* find_option(std::string_view key) const;
    void apply_unsaved_changes();

    Screen& screen;

private:
    std::vector<std::pair<std::string, Widget*>> by_key_;
    // The option widgets are the list's, not the screen's: only the list
    // draws them, and at the place the row puts them.
    std::vector<std::unique_ptr<Widget>> owned_;
};

// `OptionsSubScreen`: a title, a list of options and Done.
class OptionsSubScreen : public Screen {
public:
    OptionsSubScreen(std::shared_ptr<Screen> back, Options& settings, Component heading);

    void init() override;
    void on_close() override;
    void removed() override;

    virtual void add_title();
    virtual void add_contents();
    virtual void add_options() = 0;
    virtual void add_footer();

    Options& options;
    OptionsList* list = nullptr;
    // The list between header and footer, which is resized with the screen.
    EntryList* content = nullptr;
    HeaderAndFooterLayout* frame = nullptr;
    // Where options.txt is written; a test points it away from the player's.
    std::filesystem::path file;

protected:
    // Keeps the option widgets alive between init() calls.
    std::vector<std::unique_ptr<Widget>> owned_;
    Widget& own(std::unique_ptr<Widget> widget);
};

class OptionsScreen : public Screen {
public:
    OptionsScreen(std::shared_ptr<Screen> back, Options& settings);
    void init() override;
    void removed() override;

    Options& options;
    std::filesystem::path file;

private:
    std::shared_ptr<Screen> self_;
};

class VideoSettingsScreen : public OptionsSubScreen {
public:
    VideoSettingsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
    void tick() override;
};

class ControlsScreen : public OptionsSubScreen {
public:
    ControlsScreen(std::shared_ptr<Screen> back, Options& settings, Keybinds& binds);
    void add_options() override;

    Keybinds& keybinds;

private:
    std::shared_ptr<Screen> self_;
};

class MouseSettingsScreen : public OptionsSubScreen {
public:
    MouseSettingsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

class ChatOptionsScreen : public OptionsSubScreen {
public:
    ChatOptionsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

class SoundOptionsScreen : public OptionsSubScreen {
public:
    SoundOptionsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

class SkinCustomizationScreen : public OptionsSubScreen {
public:
    SkinCustomizationScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

class AccessibilityOptionsScreen : public OptionsSubScreen {
public:
    AccessibilityOptionsScreen(std::shared_ptr<Screen> back, Options& settings, Keybinds& binds);
    void add_options() override;
    void add_footer() override;

    Keybinds& keybinds;

private:
    std::shared_ptr<Screen> self_;
};

class FontOptionsScreen : public OptionsSubScreen {
public:
    FontOptionsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

class OnlineOptionsScreen : public OptionsSubScreen {
public:
    OnlineOptionsScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_options() override;
};

// The game's LanguageSelectionList: rows fifty wider than the usual list's.
class LanguageSelectionList : public EntryList {
public:
    LanguageSelectionList(int list_w, int list_h, int top)
        : EntryList(0, top, list_w, list_h, 18) {}
    int row_width() const override { return 220 + 50; }
};

// `LanguageSelectScreen`: the languages the client has, with a search box.
class LanguageSelectScreen : public OptionsSubScreen {
public:
    LanguageSelectScreen(std::shared_ptr<Screen> back, Options& settings);
    void add_title() override;
    void add_contents() override;
    void add_options() override {}
    void add_footer() override;

    struct Language {
        std::string code;
        std::string name;
        std::string region;
    };
    static const std::vector<Language>& languages();
    void on_done();

    EntryList* language_list = nullptr;
    EditBox* search = nullptr;

private:
    void filter(const std::string& text);
    std::shared_ptr<Screen> self_;
    std::string chosen_;
};

// `KeyBindsList`: a row per binding, 20 high and 340 wide, with the clash
// warning and the reset button.
class KeyBindsScreen;

class KeyBindsList : public EntryList {
public:
    KeyBindsList(KeyBindsScreen& owner, Keybinds& binds, int list_w, int list_h, int top);
    int row_width() const override { return 340; }
    bool entries_can_be_selected() const override { return false; }
    void refresh_entries();

    KeyBindsScreen& screen;
    Keybinds& keybinds;
};

class KeyBindsScreen : public OptionsSubScreen {
public:
    KeyBindsScreen(std::shared_ptr<Screen> back, Options& settings, Keybinds& binds);

    void add_contents() override;
    void add_options() override {}
    void add_footer() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool mouse_clicked(double mx, double my, int button) override;
    bool key_pressed(const KeyEvent& event) override;

    Keybinds& keybinds;
    // The binding waiting for a key; null when none is.
    Keybind* selected_key = nullptr;
    KeyBindsList* binds_list = nullptr;

private:
    Button* reset_button_ = nullptr;
};

} // namespace mcpp::ui
