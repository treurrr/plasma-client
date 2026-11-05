#include "mcpp/jmath.hpp"
#include "mcpp/ui/panorama.hpp"
#include "mcpp/ui/pause_screen.hpp"
#include "mcpp/ui/title_screen.hpp"

#include <cmath>
#include <format>
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

struct Box {
    int x, y, w, h;
    bool active = true;
};

void expect(const Widget& widget, Box box, std::string_view what) {
    check(widget.x == box.x && widget.y == box.y && widget.w == box.w && widget.h == box.h &&
              widget.active == box.active,
          std::format("{}: at {},{} {}x{} {} (want {},{} {}x{} {})", what, widget.x, widget.y,
                      widget.w, widget.h, widget.active, box.x, box.y, box.w, box.h, box.active));
}

template <typename S, typename... Args> S& open(ScreenManager& manager, int w, int h, Args... args) {
    manager.resize(w, h);
    auto screen = std::make_shared<S>(args...);
    manager.set_screen(screen);
    manager.apply_pending();
    return *screen;
}

// Every number below is worked by hand from TitleScreen.init: the big
// buttons from height / 4 + 48, 24 apart, the small row 36 under Realms.
void title_layout() {
    struct Case {
        int w, h;
        int left, top, row, language, half_right, accessibility;
    };
    // 854x480 at gui scale 2, and 1920x1080 at gui scale 4.
    for (const Case c : {Case{427, 240, 113, 108, 192, 89, 215, 317},
                         Case{480, 270, 140, 115, 199, 116, 242, 344}}) {
        ScreenManager manager;
        auto& screen = open<TitleScreen>(manager, c.w, c.h);
        const auto& ws = screen.widgets;
        check(ws.size() == 8, std::format("title has {} widgets", ws.size()));
        if (ws.size() != 8) continue;
        const auto at = std::format(" at {}x{}", c.w, c.h);
        expect(*ws[0], {c.left, c.top, 200, 20, false}, "singleplayer" + at);
        expect(*ws[1], {c.left, c.top + 24, 200, 20}, "multiplayer" + at);
        expect(*ws[2], {c.left, c.top + 48, 200, 20, false}, "realms" + at);
        check(c.row == c.top + 48 + 36, "the small row is 36 under realms");
        expect(*ws[3], {c.language, c.row, 20, 20}, "language" + at);
        expect(*ws[4], {c.left, c.row, 98, 20}, "options" + at);
        expect(*ws[5], {c.half_right, c.row, 98, 20}, "quit" + at);
        expect(*ws[6], {c.accessibility, c.row, 20, 20}, "accessibility" + at);
        // No font here: the copyright line is zero wide, two in from the edge.
        expect(*ws[7], {c.w - 2, c.h - 10, 0, 10}, "copyright" + at);
        check(!screen.should_close_on_esc(), "escape does not leave the title screen");
    }
}

// PauseScreen.createPauseMenu worked by hand: columns of 98 + 4 + 4, the
// first row 50 + 20 high, the rest 4 + 20, the grid 212 by 166 aligned at
// (0.5, 0.25) with the lerp truncated, each button 4 in from its cell.
void pause_layout() {
    struct Case {
        int w, h;
        int left, top;
    };
    for (const Case c : {Case{427, 240, 107, 18}, Case{480, 270, 134, 26}}) {
        ScreenManager manager;
        auto& screen = open<PauseScreen>(manager, c.w, c.h, true, false);
        const auto& ws = screen.widgets;
        check(ws.size() == 9, std::format("pause has {} widgets", ws.size()));
        if (ws.size() != 9) continue;
        const int l = c.left + 4;
        const int r = c.left + 106 + 4;
        const int y = c.top;
        const auto at = std::format(" at {}x{}", c.w, c.h);
        expect(*ws[0], {l, y + 50, 204, 20}, "back to game" + at);
        expect(*ws[1], {l, y + 70 + 4, 98, 20}, "advancements" + at);
        expect(*ws[2], {r, y + 70 + 4, 98, 20}, "statistics" + at);
        expect(*ws[3], {l, y + 94 + 4, 98, 20}, "give feedback" + at);
        expect(*ws[4], {r, y + 94 + 4, 98, 20}, "report bugs" + at);
        expect(*ws[5], {l, y + 118 + 4, 98, 20}, "options" + at);
        expect(*ws[6], {r, y + 118 + 4, 98, 20}, "player reporting" + at);
        expect(*ws[7], {l, y + 142 + 4, 204, 20}, "disconnect" + at);
        expect(*ws[8], {c.w / 2, 40, 0, 9, false}, "title" + at);
        check(screen.disconnect_button == ws[7].get(), "the disconnect button is kept");
    }
    // 854x480 at scale 2, written out: Back to Game at 111,68 and Disconnect at 111,164.
    ScreenManager manager;
    auto& screen = open<PauseScreen>(manager, 427, 240, true, false);
    expect(*screen.widgets[0], {111, 68, 204, 20}, "back to game, 854x480");
    expect(*screen.widgets[7], {111, 164, 204, 20}, "disconnect, 854x480");

    // With server links the feedback pair becomes Feedback... and Server Links...
    auto& links = open<PauseScreen>(manager, 427, 240, true, true);
    check(links.widgets.size() == 9, "pause with links has the same number of buttons");
    check(links.widgets[3]->message.text == "menu.feedback", "feedback subscreen button");
    check(links.widgets[4]->message.text == "menu.server_links", "server links button");
    check(!links.widgets[4]->tooltip.empty(), "server links carries the custom options note");

    // Without the menu (F3+Escape) only the title, 10 down.
    auto& bare = open<PauseScreen>(manager, 427, 240, false, false);
    check(bare.widgets.size() == 1 && bare.widgets[0]->y == 10, "paused title alone");

    // Back to Game and Escape both close it.
    auto& back = open<PauseScreen>(manager, 427, 240, true, false);
    static_cast<Button&>(*back.widgets[0]).press();
    manager.apply_pending();
    check(manager.current() == nullptr, "back to game closes the screen");
    open<PauseScreen>(manager, 427, 240, true, false);
    manager.key(256, 0, 1, 0);
    manager.apply_pending();
    check(manager.current() == nullptr, "escape closes the pause screen");

    bool disconnected = false;
    manager.host.disconnect = [&] { disconnected = true; };
    auto& leave = open<PauseScreen>(manager, 427, 240, true, false);
    auto& button = static_cast<Button&>(*leave.widgets[7]);
    button.press();
    check(disconnected && !button.active, "disconnect asks the host and greys itself");
}

void splash() {
    // The hidden line's hash is the constant the game filters by.
    check(java_string_hash("This message will never appear on the splash screen, isn't that "
                           "weird?") == 125780783,
          "hidden splash hash");
    check(java_string_hash("") == 0, "empty hash");
    check(java_string_hash("hello") == 99162322, "hello hash");

    // 1.8 at the top of each second, 1.7 a quarter in, scaled by 100 / (w + 32).
    check(std::abs(SplashRenderer::scale_at(0, 68) - 1.8f) < 1e-6f, "splash at 0 ms");
    check(std::abs(SplashRenderer::scale_at(250, 68) - 1.7f) < 1e-5f, "splash at 250 ms");
    check(std::abs(SplashRenderer::scale_at(1750, 68) - 1.7f) < 1e-5f, "splash at 1750 ms");
    check(std::abs(SplashRenderer::scale_at(0, 100) - 1.8f * 100.0f / 132.0f) < 1e-6f,
          "splash fits its width");

    SplashManager manager;
    manager.splashes = {SplashManager::literal_splash("a"), SplashManager::literal_splash("b")};
    jmath::LegacyRandom random(1);
    check(manager.get_splash(12, 24, "x", random)->splash.text == "Merry X-mas!", "christmas");
    check(manager.get_splash(1, 1, "x", random)->splash.text == "Happy new year!", "new year");
    check(manager.get_splash(10, 31, "x", random)->splash.text == "OOoooOOOoooo! Spooky!",
          "halloween");
    check(manager.get_splash(12, 25, "x", random)->splash.style.colour == 0xFFFF00u,
          "splashes are yellow");
    SplashManager empty;
    check(!empty.get_splash(5, 5, "x", random), "no splashes, no splash");

    // java.util.Random(1).nextInt(2) twice: 1 then 0, so not 42 then "a".
    jmath::LegacyRandom seeded(1);
    check(manager.get_splash(5, 5, "x", seeded)->splash.text == "a", "random pick");
}

void panorama() {
    check(PanoramaRenderer::wrap(360.5f, 360.0f) == 0.5f, "wrap past a turn");
    check(PanoramaRenderer::wrap(360.0f, 360.0f) == 360.0f, "wrap keeps exactly a turn");
    check(PanoramaRenderer::wrap(10.0f, 360.0f) == 10.0f, "wrap leaves the rest");
}

} // namespace

int main() {
    title_layout();
    pause_layout();
    splash();
    panorama();
    if (failures == 0) std::cout << "title: all passed\n";
    return failures == 0 ? 0 : 1;
}
