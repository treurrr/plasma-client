#include "mcpp/keybinds.hpp"
#include "mcpp/options.hpp"
#include "mcpp/ui/chat_screen.hpp"
#include "mcpp/ui/multiplayer.hpp"
#include "mcpp/ui/options_ui.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace mcpp;
using namespace mcpp::ui;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

void same(int got, int want, std::string_view what) {
    check(got == want, std::format("{}: {} (want {})", what, got, want));
}

void same(const std::string& got, const std::string& want, std::string_view what) {
    check(got == want, std::format("{}: '{}' (want '{}')", what, got, want));
}

struct Box {
    int x, y, w, h;
};

void expect(const Widget& widget, Box box, std::string_view what) {
    check(widget.x == box.x && widget.y == box.y && widget.w == box.w && widget.h == box.h,
          std::format("{}: at {},{} {}x{} (want {},{} {}x{})", what, widget.x, widget.y, widget.w,
                      widget.h, box.x, box.y, box.w, box.h));
}

template <typename S, typename... Args>
std::shared_ptr<S> open(ScreenManager& manager, int w, int h, Args&&... args) {
    manager.resize(w, h);
    auto screen = std::make_shared<S>(std::forward<Args>(args)...);
    manager.set_screen(screen);
    manager.apply_pending();
    return screen;
}

std::filesystem::path scratch() {
    static const std::filesystem::path dir = [] {
        auto base = std::filesystem::temp_directory_path() / "plasma-menus-test";
        std::filesystem::create_directories(base);
        return base;
    }();
    return dir;
}

// Every number below is worked by hand from the vanilla init() beside it, not
// read off this code.

// `JoinMultiplayerScreen.init`: at 854x480 and gui scale 2 the screen is
// 427x240 units. The header is 33 and the footer 60, so the list is 147 high
// at y 33 and 427 wide, with 305 wide rows: 427/2 - 305/2 = 61 for the left
// edge, 366 for the right and 374 for the scroll bar. The footer holds a
// vertical column of two rows 4 apart: three 100 wide buttons (308 across)
// over four 74 wide ones (also 308). The column is 44 high, centred in the 60
// of footer at 240-60+8 = 188, and 59 from the left.
void multiplayer_layout() {
    ScreenManager manager;
    Options options;
    manager.options = &options;
    auto screen = open<JoinMultiplayerScreen>(manager, 427, 240, nullptr);
    screen->file = scratch() / "servers-layout.dat";
    auto* list = screen->list;
    check(list != nullptr, "server list exists");
    if (list == nullptr) return;
    same(list->x, 0, "list x");
    same(list->y, 33, "list y");
    same(list->w, 427, "list width");
    same(list->h, 147, "list height");
    same(list->row_left(), 61, "list row left");
    same(list->row_right(), 366, "list row right");
    same(list->scroll_bar_x(), 374, "list scroll bar x");

    std::vector<const Widget*> buttons;
    for (const auto& widget : screen->widgets) {
        if (dynamic_cast<const Button*>(widget.get()) != nullptr) buttons.push_back(widget.get());
    }
    check(buttons.size() == 7, std::format("multiplayer has {} buttons", buttons.size()));
    if (buttons.size() != 7) return;
    expect(*buttons[0], {59, 188, 100, 20}, "join server");
    expect(*buttons[1], {163, 188, 100, 20}, "direct connection");
    expect(*buttons[2], {267, 188, 100, 20}, "add server");
    expect(*buttons[3], {59, 212, 74, 20}, "edit");
    expect(*buttons[4], {137, 212, 74, 20}, "delete");
    expect(*buttons[5], {215, 212, 74, 20}, "refresh");
    expect(*buttons[6], {293, 212, 74, 20}, "back");
    // Nothing is selected, so only the list can be used.
    check(!buttons[0]->active && !buttons[3]->active && !buttons[4]->active,
          "join, edit and delete start dead");

    // `ServerSelectionList` rows are 36 high; the first row's content starts
    // two inside its box, which starts two under the list's top.
    auto& servers = screen->servers;
    servers.entries.clear();
    servers.add(ServerEntry{"One", "127.0.0.1:25601", "", ServerEntry::Packs::Prompt}, false);
    servers.add(ServerEntry{"Two", "example.invalid", "", ServerEntry::Packs::Prompt}, false);
    list->update_online_servers(servers);
    check(list->size() == 3, std::format("two servers and the lan header: {}", list->size()));
    same(list->entry(0)->y, 35, "first row y");
    same(list->entry(0)->content_y(), 37, "first row content y");
    same(list->entry(0)->content_x(), 63, "first row content x");
    same(list->entry(0)->content_width(), 301, "first row content width");
    same(list->entry(1)->y, 71, "second row y");
    same(list->entry(2)->y, 107, "lan header y");
}

// `ManageServerScreen.init`: the name box at 66 and the address box at 106,
// the pack cycle at height/4 + 72, Done at +114 and Cancel at +138, all 200
// wide and 100 left of the middle. At 427x240 that is x 113 and y 132, 174,
// 198.
void manage_server_layout() {
    ScreenManager manager;
    Options options;
    manager.options = &options;
    ServerEntry entry;
    auto screen = open<ManageServerScreen>(manager, 427, 240, nullptr,
                                           translatable("manageServer.add.title"), &entry,
                                           [](bool) {});
    const auto& ws = screen->widgets;
    check(ws.size() == 5, std::format("manage server has {} widgets", ws.size()));
    if (ws.size() != 5) return;
    expect(*ws[0], {113, 66, 200, 20}, "name box");
    expect(*ws[1], {113, 106, 200, 20}, "address box");
    expect(*ws[2], {113, 132, 200, 20}, "resource pack cycle");
    expect(*ws[3], {113, 174, 200, 20}, "done");
    expect(*ws[4], {113, 198, 200, 20}, "cancel");
    // `ManageServerScreen.updateAddButtonStatus`: Done needs a valid address.
    check(!ws[3]->active, "done is dead without an address");
    screen->ip_edit->set_value("127.0.0.1");
    check(ws[3]->active, "done wakes for a valid address");
    screen->ip_edit->set_value("127.0.0.1:abc");
    check(!ws[3]->active, "done dies again for a bad address");
}

// `DirectJoinServerScreen.init`: the address box at 116, Join at height/4 +
// 108 and Cancel at +132.
void direct_join_layout() {
    ScreenManager manager;
    Options options;
    options.set_raw("lastServer", "mc.example.com");
    manager.options = &options;
    ServerEntry entry;
    auto screen =
        open<DirectJoinServerScreen>(manager, 427, 240, nullptr, &entry, [](bool) {});
    const auto& ws = screen->widgets;
    check(ws.size() == 3, std::format("direct join has {} widgets", ws.size()));
    if (ws.size() != 3) return;
    expect(*ws[0], {113, 116, 200, 20}, "address box");
    expect(*ws[1], {113, 168, 200, 20}, "join server");
    expect(*ws[2], {113, 192, 200, 20}, "cancel");
    // `DirectJoinServerScreen.init`: the box starts on lastServer.
    same(screen->ip_edit->value(), "mc.example.com", "last address remembered");
    check(ws[1]->active, "join wakes for the remembered address");
}

// `ConnectScreen.init`: only Cancel, at height/4 + 132.
void connect_layout() {
    ScreenManager manager;
    auto screen = open<ConnectScreen>(manager, 427, 240, nullptr, translatable("connect.failed"));
    check(screen->widgets.size() == 1, "connect screen has one button");
    if (screen->widgets.empty()) return;
    expect(*screen->widgets[0], {113, 192, 200, 20}, "cancel");
    check(!screen->should_close_on_esc(), "connect screen ignores escape");
}

// `DisconnectedScreen.init`: the title, the reason and one 200 wide button in
// a column centred on the screen, ten of padding around the first two and two
// around the button.
void disconnected_layout() {
    ScreenManager manager;
    auto screen = open<DisconnectedScreen>(manager, 427, 240, nullptr,
                                           translatable("disconnect.lost"),
                                           literal("Server closed"));
    check(screen->widgets.size() == 3, "disconnected screen has three widgets");
    if (screen->widgets.size() != 3) return;
    const Widget& button = *screen->widgets[2];
    same(button.w, 200, "back button width");
    same(button.h, 20, "back button height");
    // The column is 200 wide with two of padding on the button, so the button
    // is centred on the screen: (427 - 200) / 2 = 113.
    same(button.x, 113, "back button x");
    check(!screen->should_close_on_esc(), "disconnected screen ignores escape");
}

// `DeathScreen.init`: Respawn at height/4 + 72 and Title Screen at +96, both
// 200 wide; both dead until twenty ticks have gone by.
void death_layout() {
    ScreenManager manager;
    auto screen = open<DeathScreen>(manager, 427, 240, std::nullopt, false, 17);
    check(screen->widgets.size() == 2, "death screen has two buttons");
    if (screen->widgets.size() != 2) return;
    expect(*screen->widgets[0], {113, 132, 200, 20}, "respawn");
    expect(*screen->widgets[1], {113, 156, 200, 20}, "title screen");
    check(!screen->widgets[0]->active, "respawn starts dead");
    for (int tick = 0; tick < 19; ++tick) screen->tick();
    check(!screen->widgets[0]->active, "respawn is still dead after nineteen ticks");
    screen->tick();
    check(screen->widgets[0]->active, "respawn wakes on the twentieth tick");
    check(!screen->is_pause_screen(), "the death screen does not pause");
}

// `ChatScreen.init`: the input line four in from the left, the screen's width
// less four across, twelve high, along the bottom.
void chat_layout() {
    ScreenManager manager;
    Options options;
    manager.options = &options;
    auto screen = open<ChatScreen>(manager, 427, 240, "", false);
    check(screen->widgets.size() == 1, "chat screen has one widget");
    if (screen->widgets.empty()) return;
    expect(*screen->widgets[0], {4, 228, 423, 12}, "chat input");
    check(!screen->is_pause_screen(), "the chat screen does not pause");
    check(screen->input->bordered == false, "chat input has no border");
    same(static_cast<int>(screen->input->max_length), 256, "chat input length");
}

// `OptionsScreen.init`: a 61 high header with the title over the field of
// view and the online button, a grid of ten 150 wide buttons two across in
// the middle, and Done in the 33 of footer. At 427x240 the header column is
// 308 wide and 37 high, so it sits at 59,12 and its row of buttons at y 29;
// the grid is 316 by 120 and lands at 55,87 because 240 - 33 - 120 is less
// than 61 + 30; Done is at 113,214.
void options_layout() {
    ScreenManager manager;
    Options options;
    Keybinds keybinds;
    manager.options = &options;
    manager.keybinds = &keybinds;
    auto screen = open<OptionsScreen>(manager, 427, 240, nullptr, options);
    std::vector<const Widget*> boxes;
    for (const auto& widget : screen->widgets) {
        if (dynamic_cast<const StringWidget*>(widget.get()) == nullptr) {
            boxes.push_back(widget.get());
        }
    }
    check(boxes.size() == 13, std::format("options screen has {} buttons", boxes.size()));
    if (boxes.size() != 13) return;
    expect(*boxes[0], {59, 29, 150, 20}, "field of view");
    expect(*boxes[1], {217, 29, 150, 20}, "online");
    expect(*boxes[2], {59, 87, 150, 20}, "skin customization");
    expect(*boxes[3], {217, 87, 150, 20}, "music and sounds");
    expect(*boxes[4], {59, 111, 150, 20}, "video settings");
    expect(*boxes[5], {217, 111, 150, 20}, "controls");
    expect(*boxes[6], {59, 135, 150, 20}, "language");
    expect(*boxes[7], {217, 135, 150, 20}, "chat settings");
    expect(*boxes[8], {59, 159, 150, 20}, "resource packs");
    expect(*boxes[9], {217, 159, 150, 20}, "accessibility");
    expect(*boxes[10], {59, 183, 150, 20}, "telemetry");
    expect(*boxes[11], {217, 183, 150, 20}, "credits");
    // Done, 200 wide, centred in the 33 of footer: 240 - 33 + 7 = 214.
    expect(*boxes[12], {113, 214, 200, 20}, "done");
}

// `OptionsSubScreen.init`, `OptionsList`: the header and footer are 33, so
// the list is 174 high at y 33, and its rows are 310 wide: 427/2 - 310/2 = 58
// on the left, 368 on the right, 376 for the scroll bar. The first row is a
// heading, which takes nine and four with no padding above it; the next
// heading takes two lines more.
void video_settings_layout() {
    ScreenManager manager;
    Options options;
    manager.options = &options;
    auto screen = open<VideoSettingsScreen>(manager, 427, 240, nullptr, options);
    auto* list = screen->list;
    check(list != nullptr, "options list exists");
    if (list == nullptr) return;
    same(list->y, 33, "options list y");
    same(list->h, 174, "options list height");
    same(list->row_left(), 58, "options row left");
    same(list->row_right(), 368, "options row right");
    same(list->scroll_bar_x(), 376, "options scroll bar x");
    // Display header (13), resolution (25), three rows of two (25 each),
    // then the quality header, which is 9 * 2 + 9 + 4 = 31 high.
    same(list->entry(0)->h, 13, "first header height");
    same(list->entry(0)->y, 35, "first header y");
    same(list->entry(1)->y, 48, "resolution row y");
    same(list->entry(1)->h, 25, "resolution row height");
    same(list->entry(2)->y, 73, "first option row y");
    same(list->entry(5)->y, 148, "quality header y");
    same(list->entry(5)->h, 31, "quality header height");
    // Every option of the three groups: 1 resolution + 6 display in 3 rows,
    // 1 preset + 17 quality in 9 rows, 4 preferences in 2 rows, 3 headers.
    same(static_cast<int>(list->size()), 3 + 1 + 3 + 1 + 9 + 2, "video rows");
    check(list->find_option("maxFps") != nullptr, "the framerate option is in the list");
    check(list->find_option("mipmapLevels") != nullptr, "the mipmap option is in the list");
}

// `KeyBindsList`: rows 20 high and 340 wide, so 43 on the left, 383 on the
// right and 391 for the scroll bar; the reset button sits 10 left of the bar
// and the change button 5 left of that.
void key_binds_layout() {
    ScreenManager manager;
    Options options;
    Keybinds keybinds;
    keybinds.load(options);
    manager.options = &options;
    manager.keybinds = &keybinds;
    auto screen = open<KeyBindsScreen>(manager, 427, 240, nullptr, options, keybinds);
    auto* list = screen->binds_list;
    check(list != nullptr, "key binds list exists");
    if (list == nullptr) return;
    same(list->row_left(), 43, "key binds row left");
    same(list->row_right(), 383, "key binds row right");
    same(list->scroll_bar_x(), 391, "key binds scroll bar x");
    // Fifty-nine bindings over eight categories.
    same(static_cast<int>(list->size()), 59 + 8, "key binds rows");
    same(list->entry(0)->h, 20, "category row height");
    same(list->entry(0)->y, 35, "category row y");
    same(list->entry(1)->y, 55, "first binding row y");
}

// `KeyBindsList.KeyEntry.refreshEntry`: two bindings on one key, where at
// least one of them was moved there, colour the row.
void key_binds_clash() {
    ScreenManager manager;
    Options options;
    Keybinds keybinds;
    keybinds.load(options);
    manager.options = &options;
    manager.keybinds = &keybinds;
    // Jump on W, where Forward already is.
    auto* jump = keybinds.find("key.jump");
    auto* forward = keybinds.find("key.forward");
    check(jump != nullptr && forward != nullptr, "jump and forward exist");
    if (jump == nullptr || forward == nullptr) return;
    jump->current = InputKey::keyboard(87);
    check(jump->current == forward->current, "jump now shares forward's key");
    check(!jump->is_default(), "jump is no longer on its own key");
    const auto clashes = keybinds.clashes(*jump);
    check(clashes.size() == 1, std::format("jump clashes with {} bindings", clashes.size()));
    // And two bindings that are both still on their defaults do not clash:
    // the game leaves the duplicate pick-item and spectator hotbar alone.
    const auto* pick = keybinds.find("key.pickItem");
    const auto* spectator = keybinds.find("key.spectatorHotbar");
    check(pick != nullptr && spectator != nullptr, "the two middle-click bindings exist");
    if (pick != nullptr && spectator != nullptr) {
        check(pick->current == spectator->current, "both are on the middle button");
        check(pick->is_default() && spectator->is_default(), "both are on their defaults");
    }
}

// `OptionInstance.IntRangeBase`: the slider position of a value and back.
void option_sliders() {
    Options options;
    const OptionDef* fov = option_def("fov");
    check(fov != nullptr, "the field of view option exists");
    if (fov == nullptr) return;
    // The default option value is 70, written as (70 - 70) / 40 = 0.0.
    same(fov->get(options), "0.0", "field of view default");
    // minInclusive is 30, so 70 maps to (70 + 0.5 - 30) / (110 + 1 - 30) =
    // 40.5 / 81 = 0.5.
    check(std::abs(fov->to_slider("0.0") - 0.5) < 1e-9,
          std::format("field of view slider position {}", fov->to_slider("0.0")));
    // The far end: 1.0 is pushed to 0.99999f, floor(30 + 0.99999 * 81) = 110.
    same(fov->from_slider(1.0), "1.0", "field of view at the far end");
    same(fov->from_slider(0.0), "-1.0", "field of view at the near end");
    // One step right of 70 is 71, which is (71 - 70) / 40 = 0.025.
    const auto step = fov->step("0.0", 1);
    check(step.has_value() && *step == "0.025",
          std::format("one step up: {}", step.value_or("none")));

    const OptionDef* fps = option_def("maxFps");
    check(fps != nullptr && fps->get(options) == "120", "framerate default is 120");
    if (fps != nullptr) {
        // The domain is 1 to 26 and the value is ten times it, so 120 is 12:
        // (12 + 0.5 - 1) / (26 + 1 - 1) = 11.5 / 26.
        check(std::abs(fps->to_slider("120") - 11.5 / 26.0) < 1e-9, "framerate position");
        same(fps->from_slider(1.0), "260", "framerate at the far end");
        same(fps->from_slider(0.0), "10", "framerate at the near end");
    }

    const OptionDef* distance = option_def("renderDistance");
    check(distance != nullptr, "the render distance option exists");
    if (distance != nullptr) {
        // `IntRange(2, 32, false)`: no steps, so the arrows move by a pixel.
        check(!distance->step, "render distance has no steps");
        check(!distance->apply_immediately, "render distance waits before it applies");
        same(distance->from_slider(0.0), "2", "render distance at the near end");
        same(distance->from_slider(1.0), "32", "render distance at the far end");
    }

    const OptionDef* sensitivity = option_def("mouseSensitivity");
    check(sensitivity != nullptr, "the sensitivity option exists");
    if (sensitivity != nullptr) {
        // `UnitDouble`: the value is its own position.
        check(std::abs(sensitivity->to_slider("0.5") - 0.5) < 1e-12, "sensitivity position");
        same(sensitivity->from_slider(0.25), "0.25", "sensitivity written back");
    }

    // `Options.fovEffectScale`: the stored value is the square of the slider.
    const OptionDef* fov_effect = option_def("fovEffectScale");
    check(fov_effect != nullptr, "the fov effect option exists");
    if (fov_effect != nullptr) {
        same(fov_effect->from_slider(0.5), "0.25", "fov effect at half the slider");
        check(std::abs(fov_effect->to_slider("0.25") - 0.5) < 1e-12, "fov effect position");
    }

    // `ChatComponent.getWidth`, `getHeight`: the pixel labels.
    same(chat_width_px(1.0), 320, "chat width at full");
    same(chat_width_px(0.0), 40, "chat width at nothing");
    same(chat_height_px(1.0), 180, "chat height focused");
    same(chat_height_px(0.4375), 90, "chat height unfocused");
}

// Every option a screen reaches has to exist, or a screen would build a row
// out of a null.
void option_table() {
    for (const char* key :
         {"maxFps", "enableVsync", "inactivityFpsLimit", "guiScale", "fullscreen", "gamma",
          "graphicsPreset", "biomeBlendRadius", "renderDistance", "prioritizeChunkUpdates",
          "simulationDistance", "ao", "renderClouds", "particles", "mipmapLevels",
          "entityShadows", "entityDistanceScaling", "menuBackgroundBlurriness", "cloudRange",
          "cutoutLeaves", "improvedTransparency", "textureFiltering", "maxAnisotropyBit",
          "weatherRadius", "showAutosaveIndicator", "vignette", "attackIndicator",
          "chunkSectionFadeInTime", "toggleCrouch", "toggleSprint", "toggleAttack", "toggleUse",
          "autoJump", "sprintWindow", "operatorItemsTab", "mouseSensitivity", "touchscreen",
          "mouseWheelSensitivity", "discrete_mouse_scroll", "invertXMouse", "invertYMouse",
          "allowCursorChanges", "rawMouseInput", "chatVisibility", "chatColors", "chatLinks",
          "chatLinksPrompt", "chatOpacity", "textBackgroundOpacity", "chatScale",
          "chatLineSpacing", "chatDelay", "chatWidth", "chatHeightFocused",
          "chatHeightUnfocused", "narrator", "autoSuggestions", "hideMatchedNames",
          "reducedDebugInfo", "onlyShowSecureChat", "saveChatDrafts", "soundCategory_master",
          "soundCategory_ui", "soundDevice", "showSubtitles", "directionalAudio",
          "musicFrequency", "musicToast", "modelPart_cape", "modelPart_hat", "mainHand",
          "highContrast", "backgroundForChatOnly", "notificationDisplayTime", "bobView",
          "screenEffectScale", "fovEffectScale", "darknessEffectScale", "damageTiltStrength",
          "glintSpeed", "glintStrength", "hideLightningFlashes",
          "darkMojangStudiosBackground", "panoramaScrollSpeed", "hideSplashTexts",
          "narratorHotkey", "rotateWithMinecart", "highContrastBlockOutline",
          "forceUnicodeFont", "japaneseGlyphVariants", "realmsNotifications",
          "allowServerListing", "fov"}) {
        const OptionDef* def = option_def(key);
        check(def != nullptr, std::format("option {} is defined", key));
        if (def == nullptr) continue;
        // Every option has to read back as one of its own values, or the
        // button would open on nothing.
        check(Options::default_of(key) != nullptr,
              std::format("option {} has a default in options.txt", key));
        if (def->kind == OptionDef::Kind::Cycle && def->key != "soundDevice") {
            const std::string current = def->get(Options{});
            bool found = false;
            for (const auto& value : def->values) {
                if (value == current) found = true;
            }
            check(found, std::format("option {} opens on one of its values ('{}')", key, current));
        }
    }
}

// options.txt has to go out exactly as it came in: the same keys, in the same
// order, with the same text.
void options_round_trip() {
    Options fresh;
    const std::string written = fresh.serialise();
    Options again;
    again.parse(written);
    same(again.serialise(), written, "the defaults round trip");
    same(static_cast<int>(fresh.entries().size()), 185, "the file has 185 lines");

    // The player's own file, when the launcher has written one. Nothing is
    // ever written back to it; it is only read.
    if (const auto vanilla = Options::vanilla_path()) {
        std::ifstream file(*vanilla, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        Options read;
        read.parse(text);
        // Every line of the real file comes back, by key and by value.
        size_t lines = 0;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string_view line(text.data() + at, end - at);
            at = end + 1;
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0) continue;
            ++lines;
            const std::string key(line.substr(0, colon));
            const std::string value(line.substr(colon + 1));
            const auto* got = read.raw(key);
            check(got != nullptr && *got == value,
                  std::format("round trip of {}: '{}' (want '{}')", key,
                              got != nullptr ? *got : std::string("missing"), value));
        }
        check(lines > 100, std::format("the player's file has {} lines", lines));
        // And the serialised form keeps the game's own order, so vanilla
        // reads it as its own.
        const std::string out = read.serialise();
        size_t previous = 0;
        bool ordered = true;
        size_t cursor = 0;
        while (cursor < text.size()) {
            size_t end = text.find('\n', cursor);
            if (end == std::string::npos) end = text.size();
            std::string_view line(text.data() + cursor, end - cursor);
            cursor = end + 1;
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0) continue;
            const std::string needle = std::string(line.substr(0, colon)) + ":";
            const size_t found = out.find("\n" + needle);
            const size_t place = found == std::string::npos && out.compare(0, needle.size(), needle) == 0
                                     ? 0
                                     : found;
            if (place == std::string::npos || (previous != 0 && place < previous)) ordered = false;
            previous = place;
        }
        check(ordered, "the written file keeps the game's own order of keys");
    } else {
        std::cerr << "note: no launcher options.txt to compare against\n";
    }
}

// `ServerAddress.isValidAddress`.
void addresses() {
    for (const char* good : {"localhost", "127.0.0.1", "127.0.0.1:25565", "mc.example.com",
                             "mc.example.com:19132", "[::1]", "[::1]:25565",
                             // One colon with nothing after it is a host with
                             // no port, and two or more colons a bare IPv6
                             // literal: Guava takes both.
                             "127.0.0.1:", "a:1:2"}) {
        check(is_valid_address(good), std::format("'{}' is a valid address", good));
    }
    for (const char* bad : {"", ":", ":25565", "127.0.0.1:abc", "127.0.0.1:99999",
                            "127.0.0.1:+1", "[::1", "[::1]x"}) {
        check(!is_valid_address(bad), std::format("'{}' is not a valid address", bad));
    }
    check(parse_address("mc.example.com").second == 25565, "the default port is 25565");
    check(parse_address("mc.example.com:19132").second == 19132, "a written port is kept");
    check(parse_address("[::1]:25601").first == "::1", "a bracketed host loses its brackets");
}

// servers.dat has to be written back with every field this client does not
// know still in it.
void servers_dat_round_trip() {
    // A file as the game writes it, with one field this client knows nothing
    // of: acceptedCodeOfConduct, which 1.21.11 writes for a server whose code
    // the player has agreed to.
    ByteBuffer out;
    const auto name = [&out](std::string_view text) {
        out.write_u16(static_cast<uint16_t>(text.size()));
        out.write_bytes(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    };
    out.write_u8(10);   // the root compound, named as the file names it
    name("");
    out.write_u8(9);   // a list
    name("servers");
    out.write_u8(10);
    out.write_i32(1);
    out.write_u8(8);
    name("name");
    name("Bradge");
    out.write_u8(8);
    name("ip");
    name("mc.example.com");
    out.write_u8(3);
    name("acceptedCodeOfConduct");
    out.write_i32(7);
    out.write_u8(1);
    name("hidden");
    out.write_u8(0);
    out.write_u8(0);   // end of the entry
    out.write_u8(0);   // end of the root

    const auto first = ServerList::decode(out.data());
    check(first.has_value(), "the file reads");
    if (!first) return;
    check(first->entries.size() == 1, "one entry");
    same(first->entries[0].name, "Bradge", "the entry's name");
    same(first->entries[0].ip, "mc.example.com", "the entry's address");
    const auto* kept = first->entries[0].extra.find("acceptedCodeOfConduct");
    check(kept != nullptr && kept->as_int().value_or(0) == 7,
          "the unknown field is kept on the entry");

    // Written back and read again: the unknown field survives, and so does a
    // change to a field this client does know.
    ServerList changed = *first;
    changed.entries[0].name = "BradgeSMP";
    changed.entries[0].packs = ServerEntry::Packs::Enabled;
    const auto bytes = changed.encode();
    const auto second = ServerList::decode(bytes);
    check(second.has_value(), "the written file reads back");
    if (!second) return;
    check(second->entries.size() == 1, "still one entry");
    same(second->entries[0].name, "BradgeSMP", "the new name");
    same(second->entries[0].ip, "mc.example.com", "the address is unchanged");
    check(second->entries[0].packs == ServerEntry::Packs::Enabled, "the pack status is written");
    const auto* still = second->entries[0].extra.find("acceptedCodeOfConduct");
    check(still != nullptr && still->as_int().value_or(0) == 7,
          "the unknown field survived the save");
    // And writing twice gives the same bytes, so nothing drifts.
    check(second->encode() == changed.encode(), "a second save writes the same bytes");

    // The file on disk, with a save that never leaves a half-written one.
    const auto path = scratch() / "servers.dat";
    check(changed.save(path), "the file is written");
    ServerList loaded;
    check(loaded.load(path), "the file is read back");
    check(loaded.entries.size() == 1 && loaded.entries[0].name == "BradgeSMP",
          "the file on disk holds the entry");
    check(!std::filesystem::exists(path.string() + ".tmp"), "no half-written file is left");

    // `ServerList.add`: a hidden entry goes first and only sixteen are kept.
    ServerList hidden;
    for (int i = 0; i < 20; ++i) {
        hidden.add(ServerEntry{std::format("h{}", i), std::format("h{}.example", i)}, true);
    }
    check(hidden.entries.size() == 16, std::format("{} hidden entries kept", hidden.entries.size()));
    same(hidden.entries[0].name, "h19", "the newest hidden entry is first");
    check(hidden.visible().empty(), "hidden entries are not in the list");
    // `ServerList.unhide`: it comes back as a listed entry.
    check(hidden.unhide("h19.example") != nullptr, "the hidden entry comes back");
    check(hidden.visible().size() == 1, "and is in the list");
}

// `ChatScreen.normalizeChatMessage`: trimmed, with runs of spaces squeezed.
void chat_input() {
    same(ChatScreen::normalise("  hello   world  "), "hello world", "the line is squeezed");
    same(ChatScreen::normalise("\t/say  hi\n"), "/say hi", "a command is squeezed too");
    same(ChatScreen::normalise("   "), "", "a line of spaces is nothing");
    same(static_cast<int>(ChatScreen::normalise(std::string(300, 'a')).size()), 256,
          "a long line is cut to 256");
}

// `CommandSuggestions.updateCommandInfo`: the literals the tree offers for the
// word the caret is in.
void command_completion() {
    proto::CommandTree tree;
    tree.nodes.resize(6);
    tree.root = 0;
    tree.nodes[0].kind = proto::CommandNode::Kind::Root;
    tree.nodes[0].children = {1, 2, 3};
    tree.nodes[1].kind = proto::CommandNode::Kind::Literal;
    tree.nodes[1].name = "gamemode";
    tree.nodes[1].children = {4, 5};
    tree.nodes[2].kind = proto::CommandNode::Kind::Literal;
    tree.nodes[2].name = "gamerule";
    tree.nodes[3].kind = proto::CommandNode::Kind::Literal;
    tree.nodes[3].name = "tp";
    tree.nodes[4].kind = proto::CommandNode::Kind::Literal;
    tree.nodes[4].name = "creative";
    tree.nodes[5].kind = proto::CommandNode::Kind::Literal;
    tree.nodes[5].name = "survival";

    auto all = command_suggestions(tree, "/", 1);
    check(all.size() == 3, std::format("a bare slash offers {} commands", all.size()));
    if (all.size() == 3) {
        same(all[0].text, "gamemode", "sorted first");
        same(all[1].text, "gamerule", "sorted second");
        same(all[2].text, "tp", "sorted third");
    }
    auto game = command_suggestions(tree, "/game", 5);
    check(game.size() == 2, std::format("'game' offers {} commands", game.size()));
    if (!game.empty()) {
        same(static_cast<int>(game[0].start), 1, "the word starts after the slash");
        same(static_cast<int>(game[0].end), 5, "and ends at the caret");
    }
    auto modes = command_suggestions(tree, "/gamemode ", 10);
    check(modes.size() == 2, std::format("'gamemode ' offers {} values", modes.size()));
    if (modes.size() == 2) {
        same(modes[0].text, "creative", "the first game mode");
        same(static_cast<int>(modes[0].start), 10, "a new word starts at the caret");
    }
    auto one = command_suggestions(tree, "/gamemode cr", 12);
    check(one.size() == 1 && one[0].text == "creative", "'cr' narrows to one value");
    check(command_suggestions(tree, "hello", 5).empty(), "plain chat offers nothing");
    check(command_suggestions(tree, "/nosuch ", 8).empty(), "an unknown command offers nothing");
}

// `AbstractSelectionList`: rows stacked from two under the top, the scroll
// stopping where the content runs out.
void list_scrolling() {
    struct Row : EntryList::Entry {
        void render_content(GuiGraphics&, double, double, bool) override {}
    };
    EntryList list(0, 33, 427, 100, 20);
    for (int i = 0; i < 10; ++i) list.add_entry(std::make_unique<Row>());
    // Ten rows of twenty and four more is 204 of content in 100 of view.
    same(list.content_total_height(), 204, "content height");
    same(list.max_scroll(), 104, "the most it can scroll");
    same(list.entry(0)->y, 35, "the first row");
    same(list.entry(9)->y, 215, "the last row");
    list.set_scroll(1000.0);
    same(static_cast<int>(list.scroll()), 104, "the scroll stops at the end");
    same(list.entry(0)->y, -69, "the first row has moved up");
    list.set_scroll(-5.0);
    same(static_cast<int>(list.scroll()), 0, "and it stops at the start");
    // `AbstractScrollArea.scrollerHeight`: never under 32.
    same(list.scroller_height(), 49, "the thumb's height");
    // `AbstractSelectionList.scrollRate`: half a row a notch.
    list.mouse_scrolled(0, 0, 0, -1.0);
    same(static_cast<int>(list.scroll()), 10, "one notch is half a row");
}

} // namespace

int main() {
    // Nothing here is to write to the player's own files.
    const auto home = scratch() / "home";
    std::filesystem::create_directories(home);
    setenv("XDG_DATA_HOME", (home / "data").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (home / "config").c_str(), 1);

    multiplayer_layout();
    manage_server_layout();
    direct_join_layout();
    connect_layout();
    disconnected_layout();
    death_layout();
    chat_layout();
    options_layout();
    video_settings_layout();
    key_binds_layout();
    key_binds_clash();
    option_sliders();
    option_table();
    options_round_trip();
    addresses();
    servers_dat_round_trip();
    chat_input();
    command_completion();
    list_scrolling();

    if (failures != 0) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
