#pragma once
#include "mcpp/jmath.hpp"
#include "mcpp/ui/widgets.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

// The title screen and what it is drawn from: the logo, the splash and the
// screen itself. The panorama is in panorama.hpp, shared with every screen
// of the title flow.
namespace mcpp::ui {

// The yellow tilted line beside the logo.
class SplashRenderer {
public:
    explicit SplashRenderer(Component text) : splash(std::move(text)) {}
    void render(GuiGraphics& gfx, int width, float alpha) const;
    // The splash's scale at a wall clock of `millis`: a pulse between 1.7 and
    // 1.8 twice a second, shrunk to fit its width.
    static float scale_at(int64_t millis, int text_width);

    Component splash;
};

// The splash lines, read from the game's own splashes.txt at start.
class SplashManager {
public:
    static std::filesystem::path default_path();
    bool load(const std::filesystem::path& path);
    // `SplashManager.getSplash`: the day's own splash on the three special
    // days, otherwise one at random, now and then the player's own name.
    std::optional<SplashRenderer> get_splash(int month, int day, std::string_view user,
                                             jmath::LegacyRandom& random) const;
    // The splash text style: yellow.
    static Component literal_splash(std::string text);

    std::vector<Component> splashes;
};

// java.lang.String.hashCode over the UTF-16 of `utf8`.
int32_t java_string_hash(std::string_view utf8);

class LogoRenderer {
public:
    static constexpr int kDefaultHeightOffset = 30;
    LogoRenderer(bool keep_logo_through_fade, bool easter_egg)
        : show_easter_egg(easter_egg), keep_logo_through_fade(keep_logo_through_fade) {}
    // One in ten thousand logos reads "Minceraft".
    static bool roll_easter_egg(jmath::LegacyRandom& random);
    void render_logo(GuiGraphics& gfx, int width, float alpha,
                     int height_offset = kDefaultHeightOffset) const;

    bool show_easter_egg;
    bool keep_logo_through_fade;
};

// A client that only plays on servers keeps the game's layout: Singleplayer
// and Realms stay where they are, greyed out the way the game greys out a
// button it cannot offer.
class TitleScreen : public Screen {
public:
    explicit TitleScreen(bool fading = false, std::optional<LogoRenderer> logo = std::nullopt);
    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    void render_background(GuiGraphics&, double, double) override {}
    bool should_close_on_esc() const override { return false; }
    bool is_pause_screen() const override { return false; }

    std::optional<SplashRenderer> splash;
    bool fading;
    int64_t fade_in_start = 0;
    LogoRenderer logo;

private:
    int create_normal_menu_options(int y, int spacing);
};

// The splashes and the random draws behind them, shared by every title screen.
SplashManager& splash_manager();
jmath::LegacyRandom& title_random();

} // namespace mcpp::ui
