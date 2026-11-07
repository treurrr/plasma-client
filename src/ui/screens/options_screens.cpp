#include "mcpp/ui/options_ui.hpp"

#include "mcpp/translations.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mcpp::ui {
namespace {

constexpr int kBigButtonWidth = 310;
constexpr int kSmallButtonWidth = 150;
constexpr int kDefaultItemHeight = 25;
constexpr int kXOffset = 160;
constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kYellow = 0xFFFFFF00u;

int text_width(const Screen& screen, const Component& component) {
    const auto* manager = screen.manager;
    return manager != nullptr && manager->font != nullptr ? manager->font->width(component) : 0;
}

std::string translate(const Screen& screen, std::string_view key) {
    const auto* manager = screen.manager;
    return manager != nullptr && manager->font != nullptr ? manager->font->tr(key)
                                                          : std::string(key);
}

// `KeyMapping` order: the five debug bindings the game puts out of their
// alphabetical place.
int bind_order(std::string_view name) {
    if (name == "key.debug.overlay") return -2;
    if (name == "key.debug.modifier") return -1;
    if (name == "key.debug.profilingChart") return 1;
    if (name == "key.debug.fpsCharts") return 2;
    if (name == "key.debug.networkCharts") return 3;
    return 0;
}

// `OptionsList.Entry`: a row that holds one wide widget or two side by side,
// placed against the screen's middle rather than the row's.
class OptionRow : public EntryList::Entry {
public:
    OptionRow(Screen& owner, std::vector<Widget*> held) : screen(owner), widgets(std::move(held)) {}

    void render_content(GuiGraphics& gfx, double mx, double my, bool) override {
        int offset = 0;
        const int left = screen.width / 2 - 155;
        for (Widget* widget : widgets) {
            widget->set_position(left + offset, content_y());
            widget->hovered = widget->over(mx, my);
            widget->render(gfx, mx, my);
            if (widget->hovered && !widget->tooltip.empty()) {
                std::vector<std::vector<Run>> lines;
                for (const auto& line : widget->tooltip) {
                    for (auto& wrapped : gfx.wrap(line, 170)) lines.push_back(std::move(wrapped));
                }
                gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
            }
            offset += kXOffset;
        }
    }

    std::vector<Widget*> children() override { return widgets; }

    Screen& screen;
    std::vector<Widget*> widgets;
};

// `OptionsList.HeaderEntry`: a heading, pushed down by two lines when it is
// not the first row.
class HeaderRow : public EntryList::Entry {
public:
    HeaderRow(Screen& owner, Component text, int padding_top)
        : screen(owner), label(0, 0, 0, Font::kLineHeight, std::move(text)), padding(padding_top) {}

    void render_content(GuiGraphics& gfx, double mx, double my, bool) override {
        if (label.w == 0) label.w = gfx.width_of(label.message);
        label.set_position(screen.width / 2 - 155, content_y() + padding);
        label.render(gfx, mx, my);
    }

    Screen& screen;
    StringWidget label;
    int padding;
};

} // namespace

OptionSlider::OptionSlider(const OptionDef& option, Options& settings, int x_, int y_, int w_,
                           int h_)
    : Slider(x_, y_, w_, h_, option.to_slider(option.get(settings)), {}, {}), def(&option),
      options(&settings) {
    label = [this](double position) {
        return def->label(def->caption, def->from_slider(position));
    };
    on_change = [this](double) {
        if (def->apply_immediately) {
            apply_unsaved_value();
        } else {
            apply_at_ = last_millis_ + 600;
        }
    };
    message = label(value);
    if (def->tooltip) tooltip = def->tooltip(def->get(settings));
}

// `OptionInstance.OptionInstanceSliderButton.applyUnsavedValue`.
void OptionSlider::apply_unsaved_value() {
    const std::string next = def->from_slider(value);
    if (next != def->get(*options)) {
        def->set(*options, next);
        if (def->tooltip) tooltip = def->tooltip(next);
    }
}

// `OptionInstance.OptionInstanceSliderButton.resetValue`.
void OptionSlider::reset_value() {
    const double position = def->to_slider(def->get(*options));
    if (position != value) {
        value = position;
        apply_at_.reset();
        message = label(value);
    }
}

// `OptionInstance.OptionInstanceSliderButton.renderWidget`: a value that waits
// is written once the six hundred milliseconds are up.
void OptionSlider::render(GuiGraphics& gfx, double mx, double my) {
    last_millis_ = gfx.millis;
    Slider::render(gfx, mx, my);
    if (apply_at_ && gfx.millis >= *apply_at_) {
        apply_at_.reset();
        apply_unsaved_value();
        reset_value();
    }
}

bool OptionSlider::mouse_released(double mx, double my, int button) {
    const bool taken = Slider::mouse_released(mx, my, button);
    if (def->apply_immediately) reset_value();
    return taken;
}

// `OptionInstance.OptionInstanceSliderButton.keyPressed`: an option with steps
// moves by one, the rest by a pixel of the slider.
bool OptionSlider::key_pressed(const KeyEvent& event) {
    if (def->step && (event.key == key::Left || event.key == key::Right)) {
        const auto next = def->step(def->from_slider(value), event.key == key::Left ? -1 : 1);
        if (next) {
            set_value(def->to_slider(*next));
            return true;
        }
    }
    return Slider::key_pressed(event);
}

// `OptionInstance.createButton`: a cycle button or a slider, by the value set.
std::unique_ptr<Widget> OptionDef::create_button(Options& options, int x, int y, int width) const {
    if (kind == Kind::Slider) {
        return std::make_unique<OptionSlider>(*this, options, x, y, width, 20);
    }
    const std::string current = get(options);
    size_t index = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i] == current) index = i;
    }
    std::vector<Component> labels;
    labels.reserve(values.size());
    for (const auto& value : values) labels.push_back(label ? label(caption, value) : literal(value));
    const OptionDef* self = this;
    Options* settings = &options;
    auto button = std::make_unique<CycleButton>(
        x, y, width, 20, caption, std::move(labels), index, [self, settings](size_t chosen) {
            if (chosen < self->values.size()) self->set(*settings, self->values[chosen]);
        });
    if (tooltip) button->tooltip = tooltip(current);
    return button;
}

OptionsList::OptionsList(Screen& owner, int list_w, int list_h, int top)
    : EntryList(0, top, list_w, list_h, kDefaultItemHeight), screen(owner) {}

void OptionsList::add_big(const OptionDef& def, Options& options) {
    auto widget = def.create_button(options, 0, 0, kBigButtonWidth);
    Widget* raw = widget.get();
    by_key_.emplace_back(def.key, raw);
    owned_.push_back(std::move(widget));
    add_entry(std::make_unique<OptionRow>(screen, std::vector<Widget*>{raw}));
}

// `OptionsList.addSmall`: two options to a row.
void OptionsList::add_small(const std::vector<const OptionDef*>& defs, Options& options) {
    for (size_t i = 0; i < defs.size(); i += 2) {
        std::vector<Widget*> row;
        for (size_t j = i; j < std::min(i + 2, defs.size()); ++j) {
            if (defs[j] == nullptr) continue;
            auto widget = defs[j]->create_button(options, 0, 0, kSmallButtonWidth);
            row.push_back(widget.get());
            by_key_.emplace_back(defs[j]->key, widget.get());
            owned_.push_back(std::move(widget));
        }
        add_entry(std::make_unique<OptionRow>(screen, std::move(row)));
    }
}

void OptionsList::add_small_widgets(std::vector<Widget*> widgets) {
    for (size_t i = 0; i < widgets.size(); i += 2) {
        std::vector<Widget*> row;
        for (size_t j = i; j < std::min(i + 2, widgets.size()); ++j) row.push_back(widgets[j]);
        add_entry(std::make_unique<OptionRow>(screen, std::move(row)));
    }
}

// `OptionsList.addHeader`: nothing above the first heading, two lines above
// the rest, and four under it.
void OptionsList::add_header(Component text) {
    const int padding = children().empty() ? 0 : Font::kLineHeight * 2;
    add_entry(std::make_unique<HeaderRow>(screen, std::move(text), padding),
              padding + Font::kLineHeight + 4);
}

Widget* OptionsList::find_option(std::string_view key) const {
    for (const auto& [name, widget] : by_key_) {
        if (name == key) return widget;
    }
    return nullptr;
}

// `OptionsList.applyUnsavedChanges`: the sliders that were waiting write now.
void OptionsList::apply_unsaved_changes() {
    for (const auto& [name, widget] : by_key_) {
        if (auto* slider = dynamic_cast<OptionSlider*>(widget)) slider->apply_unsaved_value();
    }
}

OptionsSubScreen::OptionsSubScreen(std::shared_ptr<Screen> back, Options& settings,
                                   Component heading)
    : Screen(std::move(heading)), options(settings), file(Options::default_path()) {
    parent = std::move(back);
}

Widget& OptionsSubScreen::own(std::unique_ptr<Widget> widget) {
    Widget& out = *widget;
    owned_.push_back(std::move(widget));
    return out;
}

// `OptionsSubScreen.init`: the title, the list and the footer, then arranged.
void OptionsSubScreen::init() {
    owned_.clear();
    auto& header_footer = layout<HeaderAndFooterLayout>(width, height);
    frame = &header_footer;
    add_title();
    add_contents();
    add_footer();
    header_footer.arrange_elements();
    if (content != nullptr) {
        content->update_size(width, header_footer.content_height(),
                             header_footer.header_height());
    }
}

// `OptionsSubScreen.addTitle`, `HeaderAndFooterLayout.addTitleHeader`.
void OptionsSubScreen::add_title() {
    const int w = text_width(*this, title);
    frame->add_to_header(add<StringWidget>(0, 0, w, 9, title));
}

void OptionsSubScreen::add_contents() {
    auto& rows = add<OptionsList>(*this, width, frame->content_height(), frame->header_height());
    rows.in_world = manager != nullptr && manager->in_world;
    list = &rows;
    content = &rows;
    frame->add_to_contents(rows);
    add_options();
}

void OptionsSubScreen::add_footer() {
    frame->add_to_footer(
        add<Button>(0, 0, 200, 20, translatable("gui.done"), [this](Button&) { on_close(); }));
}

// `OptionsSubScreen.onClose`: the waiting sliders apply before the screen goes.
void OptionsSubScreen::on_close() {
    if (list != nullptr) list->apply_unsaved_changes();
    if (manager != nullptr) manager->set_screen(parent);
}

void OptionsSubScreen::removed() { options.save(file); }

OptionsScreen::OptionsScreen(std::shared_ptr<Screen> back, Options& settings)
    : Screen(translatable("options.title")), options(settings), file(Options::default_path()) {
    parent = std::move(back);
}

// `OptionsScreen.init`: in a 61 high header the title with the field of view and the online button under it, in the middle a grid of ten buttons two across, and Done in the footer.
void OptionsScreen::init() {
    auto& frame = layout<HeaderAndFooterLayout>(width, height, 61, 33);
    auto& header = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    header.spacing(8);
    frame.add_to_header(header);
    LayoutSettings centred;
    centred.align_horizontally(0.5f);
    const int title_w = text_width(*this, title);
    header.add_child(add<StringWidget>(0, 0, title_w, 9, title), centred);
    auto& top = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    top.spacing(8);
    header.add_child(top);
    if (const OptionDef* fov = option_def("fov")) {
        auto widget = fov->create_button(options, 0, 0, 150);
        Widget& raw = *widget;
        widgets.push_back(std::move(widget));
        top.add_child(raw);
    }
    top.add_child(add<Button>(width / 2 + 5, height / 6 - 12 + 24, 150, 20,
                              translatable("options.online"), [this](Button&) {
                                  if (manager == nullptr) return;
                                  self_ = manager->current_shared();
                                  manager->set_screen(
                                      std::make_shared<OnlineOptionsScreen>(self_, options));
                              }));

    auto& grid = layout<GridLayout>();
    grid.default_cell_setting().padding(4, 0, 4, 4).align_horizontally(0.5f);
    auto row = grid.create_row_helper(2);
    const auto open = [this, &row](const char* key, auto make) {
        row.add_child(add<Button>(0, 0, 150, 20, translatable(key), [this, make](Button&) {
            if (manager == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(make(self_));
        }));
    };
    open("options.skinCustomisation", [this](std::shared_ptr<Screen> back) {
        return std::make_shared<SkinCustomizationScreen>(std::move(back), options);
    });
    open("options.sounds", [this](std::shared_ptr<Screen> back) {
        return std::make_shared<SoundOptionsScreen>(std::move(back), options);
    });
    open("options.video", [this](std::shared_ptr<Screen> back) {
        return std::make_shared<VideoSettingsScreen>(std::move(back), options);
    });
    open("options.controls", [this](std::shared_ptr<Screen> back) -> std::shared_ptr<Screen> {
        if (manager == nullptr || manager->keybinds == nullptr) return nullptr;
        return std::make_shared<ControlsScreen>(std::move(back), options, *manager->keybinds);
    });
    open("options.language", [this](std::shared_ptr<Screen> back) {
        return std::make_shared<LanguageSelectScreen>(std::move(back), options);
    });
    open("options.chat", [this](std::shared_ptr<Screen> back) {
        return std::make_shared<ChatOptionsScreen>(std::move(back), options);
    });
    row.add_child(add<Button>(0, 0, 150, 20, translatable("options.resourcepack"),
                              [](Button&) {}))
        .active = false;
    open("options.accessibility", [this](std::shared_ptr<Screen> back) -> std::shared_ptr<Screen> {
        if (manager == nullptr || manager->keybinds == nullptr) return nullptr;
        return std::make_shared<AccessibilityOptionsScreen>(std::move(back), options,
                                                            *manager->keybinds);
    });
    auto& telemetry =
        row.add_child(add<Button>(0, 0, 150, 20, translatable("options.telemetry"), [](Button&) {}));
    telemetry.active = false;
    telemetry.tooltip = {translatable("options.telemetry.disabled")};
    row.add_child(add<Button>(0, 0, 150, 20, translatable("options.credits_and_attribution"),
                              [](Button&) {}))
        .active = false;
    frame.add_to_contents(grid);
    frame.add_to_footer(
        add<Button>(0, 0, 200, 20, translatable("gui.done"), [this](Button&) { on_close(); }));
    frame.arrange_elements();
}

void OptionsScreen::removed() { options.save(file); }

VideoSettingsScreen::VideoSettingsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.videoTitle")) {}

// `VideoSettingsScreen.addOptions`: the three headed groups, with the
// resolution and the graphics preset across the whole row.
void VideoSettingsScreen::add_options() {
    const auto defs = [](std::initializer_list<const char*> keys) {
        std::vector<const OptionDef*> out;
        for (const char* key : keys) out.push_back(option_def(key));
        return out;
    };
    list->add_header(translatable("options.video.display.header"));
    // `VideoSettingsScreen.addOptions`: the fullscreen resolution, which this
    // client does not choose for the window yet.
    {
        static const OptionDef resolution = [] {
            OptionDef def;
            def.key = "fullscreenResolution";
            def.caption = translatable("options.fullscreen.resolution");
            def.values = {"-1"};
            def.label = [](const Component&, const std::string&) {
                return translatable("options.fullscreen.unavailable");
            };
            return def;
        }();
        auto widget = resolution.create_button(options, 0, 0, 310);
        widget->active = false;
        Widget* raw = &own(std::move(widget));
        list->add_small_widgets({raw});
    }
    list->add_small(defs({"maxFps", "enableVsync", "inactivityFpsLimit", "guiScale", "fullscreen",
                          "gamma"}),
                    options);
    list->add_header(translatable("options.video.quality.header"));
    list->add_big(*option_def("graphicsPreset"), options);
    list->add_small(defs({"biomeBlendRadius", "renderDistance", "prioritizeChunkUpdates",
                          "simulationDistance", "ao", "renderClouds", "particles", "mipmapLevels",
                          "entityShadows", "entityDistanceScaling", "menuBackgroundBlurriness",
                          "cloudRange", "cutoutLeaves", "improvedTransparency", "textureFiltering",
                          "maxAnisotropyBit", "weatherRadius"}),
                    options);
    list->add_header(translatable("options.video.preferences.header"));
    list->add_small(defs({"showAutosaveIndicator", "vignette", "attackIndicator",
                          "chunkSectionFadeInTime"}),
                    options);
}

// `VideoSettingsScreen.tick`: the anisotropy slider only works for that filter.
void VideoSettingsScreen::tick() {
    Screen::tick();
    if (list == nullptr) return;
    if (Widget* widget = list->find_option("maxAnisotropyBit")) {
        widget->active = options.get_int("textureFiltering") == 2;
    }
}

ControlsScreen::ControlsScreen(std::shared_ptr<Screen> back, Options& settings, Keybinds& binds)
    : OptionsSubScreen(std::move(back), settings, translatable("controls.title")),
      keybinds(binds) {}

// `ControlsScreen.addOptions`: Mouse Settings and Key Binds in the first row.
void ControlsScreen::add_options() {
    auto mouse = std::make_unique<Button>(
        0, 0, 150, 20, translatable("options.mouse_settings"), [this](Button&) {
            if (manager == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<MouseSettingsScreen>(self_, options));
        });
    auto binds = std::make_unique<Button>(
        0, 0, 150, 20, translatable("controls.keybinds"), [this](Button&) {
            if (manager == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<KeyBindsScreen>(self_, options, keybinds));
        });
    Widget* a = &own(std::move(mouse));
    Widget* b = &own(std::move(binds));
    list->add_small_widgets({a, b});
    std::vector<const OptionDef*> defs;
    for (const char* key : {"toggleCrouch", "toggleSprint", "toggleAttack", "toggleUse",
                            "autoJump", "sprintWindow", "operatorItemsTab"}) {
        defs.push_back(option_def(key));
    }
    list->add_small(defs, options);
}

MouseSettingsScreen::MouseSettingsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.mouse_settings.title")) {}

void MouseSettingsScreen::add_options() {
    std::vector<const OptionDef*> defs;
    for (const char* key : {"mouseSensitivity", "touchscreen", "mouseWheelSensitivity",
                            "discrete_mouse_scroll", "invertXMouse", "invertYMouse",
                            "allowCursorChanges", "rawMouseInput"}) {
        defs.push_back(option_def(key));
    }
    list->add_small(defs, options);
}

ChatOptionsScreen::ChatOptionsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.chat.title")) {}

void ChatOptionsScreen::add_options() {
    std::vector<const OptionDef*> defs;
    for (const char* key : {"chatVisibility", "chatColors", "chatLinks", "chatLinksPrompt",
                            "chatOpacity", "textBackgroundOpacity", "chatScale",
                            "chatLineSpacing", "chatDelay", "chatWidth", "chatHeightFocused",
                            "chatHeightUnfocused", "narrator", "autoSuggestions",
                            "hideMatchedNames", "reducedDebugInfo", "onlyShowSecureChat",
                            "saveChatDrafts"}) {
        defs.push_back(option_def(key));
    }
    list->add_small(defs, options);
}

SoundOptionsScreen::SoundOptionsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.sounds.title")) {}

// `SoundOptionsScreen.addOptions`: the master volume across the row, the other
// ten in pairs, then the device, the subtitles and the music settings.
void SoundOptionsScreen::add_options() {
    list->add_big(*option_def("soundCategory_master"), options);
    std::vector<const OptionDef*> rest;
    for (const char* source : {"music", "record", "weather", "block", "hostile", "neutral",
                               "player", "ambient", "voice", "ui"}) {
        rest.push_back(option_def(std::string("soundCategory_") + source));
    }
    list->add_small(rest, options);
    list->add_big(*option_def("soundDevice"), options);
    list->add_small({option_def("showSubtitles"), option_def("directionalAudio")}, options);
    list->add_small({option_def("musicFrequency"), option_def("musicToast")}, options);
}

SkinCustomizationScreen::SkinCustomizationScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings,
                       translatable("options.skinCustomisation.title")) {}

// `SkinCustomizationScreen.addOptions`: the seven model parts and the main hand.
void SkinCustomizationScreen::add_options() {
    std::vector<const OptionDef*> defs;
    for (const char* part : {"cape", "jacket", "left_sleeve", "right_sleeve", "left_pants_leg",
                             "right_pants_leg", "hat"}) {
        defs.push_back(option_def(std::string("modelPart_") + part));
    }
    defs.push_back(option_def("mainHand"));
    list->add_small(defs, options);
}

AccessibilityOptionsScreen::AccessibilityOptionsScreen(std::shared_ptr<Screen> back,
                                                       Options& settings, Keybinds& binds)
    : OptionsSubScreen(std::move(back), settings, translatable("options.accessibility.title")),
      keybinds(binds) {}

// `AccessibilityOptionsScreen.addOptions`: the narrator shares its row with
// the Controls button, and the rest follow in pairs.
void AccessibilityOptionsScreen::add_options() {
    auto controls = std::make_unique<Button>(
        0, 0, 150, 20, translatable("options.controls"), [this](Button&) {
            if (manager == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<ControlsScreen>(self_, options, keybinds));
        });
    auto narrator = option_def("narrator")->create_button(options, 0, 0, 150);
    Widget* a = &own(std::move(narrator));
    Widget* b = &own(std::move(controls));
    list->add_small_widgets({a, b});
    std::vector<const OptionDef*> defs;
    for (const char* key : {"showSubtitles", "highContrast", "menuBackgroundBlurriness",
                            "textBackgroundOpacity", "backgroundForChatOnly", "chatOpacity",
                            "chatLineSpacing", "chatDelay", "notificationDisplayTime", "bobView",
                            "screenEffectScale", "fovEffectScale", "darknessEffectScale",
                            "damageTiltStrength", "glintSpeed", "glintStrength",
                            "hideLightningFlashes", "darkMojangStudiosBackground",
                            "panoramaScrollSpeed", "hideSplashTexts", "narratorHotkey",
                            "rotateWithMinecart", "highContrastBlockOutline"}) {
        defs.push_back(option_def(key));
    }
    list->add_small(defs, options);
}

// `AccessibilityOptionsScreen.addFooter`: the help link beside Done.
void AccessibilityOptionsScreen::add_footer() {
    auto& row = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    row.spacing(8);
    frame->add_to_footer(row);
    row.add_child(add<Button>(0, 0, 150, 20, translatable("options.accessibility.link"),
                              [](Button&) {}));
    row.add_child(add<Button>(0, 0, 150, 20, translatable("gui.done"), [this](Button&) {
        if (manager != nullptr) manager->set_screen(parent);
    }));
}

FontOptionsScreen::FontOptionsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.font.title")) {}

void FontOptionsScreen::add_options() {
    list->add_small({option_def("forceUnicodeFont"), option_def("japaneseGlyphVariants")},
                    options);
}

OnlineOptionsScreen::OnlineOptionsScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.online.title")) {}

void OnlineOptionsScreen::add_options() {
    list->add_small({option_def("realmsNotifications"), option_def("allowServerListing")},
                    options);
}

namespace {

// `LanguageSelectScreen.LanguageSelectionList.Entry`: the language's own name,
// centred.
class LanguageRow : public EntryList::Entry {
public:
    LanguageRow(const EntryList& owner, std::string language_code, Component text)
        : list(owner), code(std::move(language_code)), label(std::move(text)) {}

    void render_content(GuiGraphics& gfx, double, double, bool) override {
        gfx.draw_centered_string(label, list.x + list.w / 2,
                                 content_y_middle() - Font::kLineHeight / 2, kWhite);
    }
    Component narration() const override { return translatable("narrator.select", {label}); }

    const EntryList& list;
    std::string code;
    Component label;
};

} // namespace

LanguageSelectScreen::LanguageSelectScreen(std::shared_ptr<Screen> back, Options& settings)
    : OptionsSubScreen(std::move(back), settings, translatable("options.language.title")) {}

// `LanguageManager.getLanguages`: the languages this client ships. Only the
// one the texts are baked for is here until more are baked.
const std::vector<LanguageSelectScreen::Language>& LanguageSelectScreen::languages() {
    static const std::vector<Language> all = {{"en_us", "English", "United States"}};
    return all;
}

// `LanguageSelectScreen.addTitle`: the title over a 200 by 15 search box, the header 36 high.
void LanguageSelectScreen::add_title() {
    auto& column = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    column.spacing(4);
    column.default_cell_setting().align_horizontally(0.5f);
    const int title_w = text_width(*this, title);
    column.add_child(add<StringWidget>(0, 0, title_w, 9, title));
    auto& box = add<EditBox>(0, 0, 200, 15, literal(""));
    search = &box;
    box.hint = translatable("gui.language.search");
    box.on_change = [this](const std::string& text) { filter(text); };
    if (manager != nullptr) box.platform = &manager->platform;
    column.add_child(box);
    frame->add_to_header(column);
    frame->set_header_height(static_cast<int>(12.0 + 9.0 + 15.0));
    frame->set_footer_height(53);
}

// `LanguageSelectScreen.addContents`: the list of languages, rows 18 high.
void LanguageSelectScreen::add_contents() {
    auto& rows = add<EntryList>(0, frame->header_height(), width, height - 33 - 53, 18);
    rows.in_world = manager != nullptr && manager->in_world;
    language_list = &rows;
    content = &rows;
    frame->add_to_contents(rows);
    chosen_ = options.get_string("lang");
    filter("");
}

void LanguageSelectScreen::filter(const std::string& text) {
    if (language_list == nullptr) return;
    std::string wanted;
    for (const char c : text) wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    language_list->clear_entries();
    std::optional<size_t> selected;
    for (const auto& language : languages()) {
        std::string name;
        for (const char c : language.name + language.region) {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!wanted.empty() && name.find(wanted) == std::string::npos) continue;
        const size_t at = language_list->add_entry(std::make_unique<LanguageRow>(
            *language_list, language.code,
            literal(language.name + " (" + language.region + ")")));
        if (language.code == chosen_) selected = at;
    }
    if (selected) {
        language_list->set_selected(selected);
        language_list->center_scroll_on(*selected);
    }
    language_list->refresh_scroll();
}

// `LanguageSelectScreen.addFooter`: the accuracy warning over the Font button and Done.
void LanguageSelectScreen::add_footer() {
    auto& column = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    column.spacing(8);
    column.default_cell_setting().align_horizontally(0.5f);
    Style grey;
    grey.colour = 0xBABABAu;
    const Component warning = translatable("options.languageAccuracyWarning", {}, grey);
    column.add_child(add<StringWidget>(0, 0, text_width(*this, warning), 9, warning));
    auto& row = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    row.spacing(8);
    column.add_child(row);
    row.add_child(add<Button>(0, 0, 150, 20, translatable("options.font"), [this](Button&) {
        if (manager == nullptr) return;
        self_ = manager->current_shared();
        manager->set_screen(std::make_shared<FontOptionsScreen>(self_, options));
    }));
    row.add_child(add<Button>(0, 0, 150, 20, translatable("gui.done"),
                              [this](Button&) { on_done(); }));
    frame->add_to_footer(column);
}

// `LanguageSelectScreen.onDone`: the chosen language is written and the packs
// are read again.
void LanguageSelectScreen::on_done() {
    if (language_list != nullptr) {
        if (auto* row = dynamic_cast<LanguageRow*>(language_list->selected())) {
            if (row->code != chosen_) options.set_raw("lang", row->code);
        }
    }
    if (manager != nullptr) manager->set_screen(parent);
}

namespace {

// `KeyBindsList.CategoryEntry`: the category's name, centred at the bottom of
// its row.
class CategoryRow : public EntryList::Entry {
public:
    CategoryRow(const EntryList& owner, Component text)
        : list(owner), label(std::move(text)) {}

    void render_content(GuiGraphics& gfx, double, double, bool) override {
        // `FocusableTextWidget`: one line plus four of padding on each side,
        // so the text sits thirteen above the row's bottom.
        const int w = gfx.width_of(label);
        gfx.draw_string(label, list.w / 2 - w / 2, content_bottom() - Font::kLineHeight - 4,
                        kWhite);
    }
    Component narration() const override { return label; }

    const EntryList& list;
    Component label;
};

// `KeyBindsList.KeyEntry`: the binding's name, the key it is on, Reset, and
// the yellow bar when two bindings share a key.
class KeyRow : public EntryList::Entry {
public:
    KeyRow(KeyBindsList& owner, Keybind& bind);

    void render_content(GuiGraphics& gfx, double mx, double my, bool) override;
    std::vector<Widget*> children() override { return {&change_, &reset_}; }
    void refresh() override;
    Component narration() const override { return name_; }

private:
    KeyBindsList& list_;
    Keybind& bind_;
    Component name_;
    Button change_;
    Button reset_;
    bool collision_ = false;
};

KeyRow::KeyRow(KeyBindsList& owner, Keybind& bind)
    : list_(owner), bind_(bind), name_(translatable(bind.name)),
      change_(0, 0, 75, 20, translatable(bind.name),
              [this](Button&) {
                  list_.screen.selected_key = &bind_;
                  list_.refresh_entries();
              }),
      reset_(0, 0, 50, 20, translatable("controls.reset"), [this](Button&) {
          bind_.current = bind_.fallback;
          list_.refresh_entries();
      }) {
    refresh();
}

// `KeyBindsList.KeyEntry.refreshEntry`: the key's name, in yellow brackets
// when it clashes and in yellow arrows while it waits for a key.
void KeyRow::refresh() {
    Style yellow;
    yellow.colour = 0xFFFF55u;
    Style white;
    white.colour = 0xFFFFFFu;
    Component key_name = bind_.current.unbound() ? translatable("key.keyboard.unknown")
                                                 : translatable(bind_.current.name());
    reset_.active = !bind_.is_default();
    collision_ = false;
    std::vector<Component> clashing;
    if (!bind_.current.unbound()) {
        for (const auto& other : list_.keybinds.all()) {
            if (&other == &bind_) continue;
            if (other.current != bind_.current) continue;
            if (other.is_default() && bind_.is_default()) continue;
            collision_ = true;
            clashing.push_back(translatable(other.name));
        }
    }
    if (collision_) {
        Component names;
        for (size_t i = 0; i < clashing.size(); ++i) {
            if (i > 0) names.extra.push_back(literal(", "));
            names.extra.push_back(clashing[i]);
        }
        Component inner = key_name;
        inner.style = white;
        Component wrapped = literal("[ ", yellow);
        wrapped.extra.push_back(inner);
        wrapped.extra.push_back(literal(" ]"));
        key_name = wrapped;
        change_.tooltip = {translatable("controls.keybinds.duplicateKeybinds", {names})};
    } else {
        change_.tooltip.clear();
    }
    if (list_.screen.selected_key == &bind_) {
        Style underlined = white;
        underlined.underlined = true;
        Component inner = key_name;
        inner.style = underlined;
        Component wrapped = literal("> ", yellow);
        wrapped.extra.push_back(inner);
        wrapped.extra.push_back(literal(" <"));
        key_name = wrapped;
    }
    change_.message = key_name;
}

void KeyRow::render_content(GuiGraphics& gfx, double mx, double my, bool) {
    const int reset_x = list_.scroll_bar_x() - reset_.w - 10;
    const int row_y = content_y() - 2;
    reset_.set_position(reset_x, row_y);
    reset_.hovered = reset_.over(mx, my);
    reset_.render(gfx, mx, my);
    const int change_x = reset_x - 5 - change_.w;
    change_.set_position(change_x, row_y);
    change_.hovered = change_.over(mx, my);
    change_.render(gfx, mx, my);
    gfx.draw_string(name_, content_x(), content_y_middle() - Font::kLineHeight / 2, kWhite);
    if (collision_) {
        const int bar = change_.x - 6;
        gfx.fill(bar, content_y() - 1, bar + 3, content_bottom(), kYellow);
    }
    if (change_.hovered && !change_.tooltip.empty()) {
        std::vector<std::vector<Run>> lines;
        for (const auto& line : change_.tooltip) {
            for (auto& wrapped : gfx.wrap(line, 170)) lines.push_back(std::move(wrapped));
        }
        gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
    }
}

} // namespace

// `KeyBindsList.KeyBindsList`: the bindings sorted by category, then by order,
// then by the name the player reads, with a row per category.
KeyBindsList::KeyBindsList(KeyBindsScreen& owner, Keybinds& binds, int list_w, int list_h, int top)
    : EntryList(0, top, list_w, list_h, 20), screen(owner), keybinds(binds) {
    const auto& order = Keybinds::categories();
    const auto category_index = [&order](std::string_view category) {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == category) return static_cast<int>(i);
        }
        return static_cast<int>(order.size());
    };
    std::vector<Keybind*> sorted;
    for (auto& bind : binds.all()) sorted.push_back(&bind);
    const std::string* previous = nullptr;
    std::sort(sorted.begin(), sorted.end(), [&](const Keybind* a, const Keybind* b) {
        const int ca = category_index(a->category);
        const int cb = category_index(b->category);
        if (ca != cb) return ca < cb;
        const int oa = bind_order(a->name);
        const int ob = bind_order(b->name);
        if (oa != ob) return oa < ob;
        return translate(screen, a->name) < translate(screen, b->name);
    });
    for (Keybind* bind : sorted) {
        if (previous == nullptr || *previous != bind->category) {
            previous = &bind->category;
            add_entry(std::make_unique<CategoryRow>(*this, translatable(bind->category)));
        }
        add_entry(std::make_unique<KeyRow>(*this, *bind));
    }
}

void KeyBindsList::refresh_entries() {
    for (const auto& entry : children()) entry->refresh();
}

KeyBindsScreen::KeyBindsScreen(std::shared_ptr<Screen> back, Options& settings, Keybinds& binds)
    : OptionsSubScreen(std::move(back), settings, translatable("controls.keybinds.title")),
      keybinds(binds) {}

void KeyBindsScreen::add_contents() {
    auto& rows = add<KeyBindsList>(*this, keybinds, width, frame->content_height(),
                                   frame->header_height());
    rows.in_world = manager != nullptr && manager->in_world;
    binds_list = &rows;
    content = &rows;
    frame->add_to_contents(rows);
}

// `KeyBindsScreen.addFooter`: Reset All beside Done.
void KeyBindsScreen::add_footer() {
    auto& row = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    row.spacing(8);
    frame->add_to_footer(row);
    reset_button_ = &row.add_child(
        add<Button>(0, 0, 150, 20, translatable("controls.resetAll"), [this](Button&) {
            for (auto& bind : keybinds.all()) bind.current = bind.fallback;
            if (binds_list != nullptr) binds_list->refresh_entries();
        }));
    row.add_child(add<Button>(0, 0, 150, 20, translatable("gui.done"),
                              [this](Button&) { on_close(); }));
}

// `KeyBindsScreen.render`: Reset All only works while something is not default.
void KeyBindsScreen::render(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    bool any = false;
    for (const auto& bind : keybinds.all()) {
        if (!bind.is_default()) {
            any = true;
            break;
        }
    }
    if (reset_button_ != nullptr) reset_button_->active = any;
}

// `KeyBindsScreen.mouseClicked`: a click puts the waiting binding on that button.
bool KeyBindsScreen::mouse_clicked(double mx, double my, int button) {
    if (selected_key != nullptr) {
        selected_key->current = InputKey::mouse(button);
        selected_key = nullptr;
        keybinds.store(options);
        if (binds_list != nullptr) binds_list->refresh_entries();
        return true;
    }
    return Screen::mouse_clicked(mx, my, button);
}

// `KeyBindsScreen.keyPressed`: Escape unbinds, anything else binds.
bool KeyBindsScreen::key_pressed(const KeyEvent& event) {
    if (selected_key != nullptr) {
        selected_key->current = event.key == key::Escape ? InputKey{InputKey::Type::Keyboard, -1}
                                                         : InputKey::keyboard(event.key);
        selected_key = nullptr;
        keybinds.store(options);
        if (binds_list != nullptr) binds_list->refresh_entries();
        return true;
    }
    return Screen::key_pressed(event);
}

} // namespace mcpp::ui
