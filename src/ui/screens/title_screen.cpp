#include "mcpp/ui/title_screen.hpp"

#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>

namespace mcpp::ui {
namespace {

int64_t unique_seed() {
    static int64_t uniquifier = 8682522807148012LL;
    uniquifier *= 1181783497276652981LL;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return uniquifier ^ std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

// Java's String.trim: every character up to a space off both ends.
std::string java_trim(std::string_view line) {
    size_t a = 0;
    size_t b = line.size();
    while (a < b && static_cast<unsigned char>(line[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(line[b - 1]) <= ' ') --b;
    return std::string(line.substr(a, b - a));
}

uint32_t white(float alpha) {
    const auto a = static_cast<uint32_t>(std::floor(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    return (a << 24) | 0xFFFFFFu;
}

} // namespace

int32_t java_string_hash(std::string_view utf8) {
    uint32_t hash = 0;
    for (size_t at = 0; at < utf8.size();) {
        const uint32_t code = next_codepoint(utf8, at);
        if (code >= 0x10000) {
            const uint32_t v = code - 0x10000;
            hash = hash * 31u + (0xD800u + (v >> 10));
            hash = hash * 31u + (0xDC00u + (v & 0x3FFu));
        } else {
            hash = hash * 31u + code;
        }
    }
    return static_cast<int32_t>(hash);
}

// `SplashManager.literalSplash`: the splash's yellow.
Component SplashManager::literal_splash(std::string text) {
    Style style;
    style.colour = 0xFFFF00u;
    return literal(std::move(text), style);
}

std::filesystem::path SplashManager::default_path() {
    return Translations::default_path().parent_path().parent_path() / "texts" / "splashes.txt";
}

// `SplashManager.prepare`: every line trimmed, the one line that would say it never shows left out.
bool SplashManager::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    splashes.clear();
    for (std::string line; std::getline(in, line);) {
        std::string trimmed = java_trim(line);
        if (java_string_hash(trimmed) == 125780783) continue;
        splashes.push_back(literal_splash(std::move(trimmed)));
    }
    return true;
}

// `SplashManager.getSplash`: the special days, else one at random, one time in the list's length the player's own name.
std::optional<SplashRenderer> SplashManager::get_splash(int month, int day, std::string_view user,
                                                        jmath::LegacyRandom& random) const {
    if (month == 12 && day == 24) return SplashRenderer(literal_splash("Merry X-mas!"));
    if (month == 1 && day == 1) return SplashRenderer(literal_splash("Happy new year!"));
    if (month == 10 && day == 31) return SplashRenderer(literal_splash("OOoooOOOoooo! Spooky!"));
    if (splashes.empty()) return std::nullopt;
    const auto count = static_cast<int32_t>(splashes.size());
    if (random.next_int(count) == 42) {
        std::string upper(user);
        for (char& c : upper) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return SplashRenderer(literal_splash(upper + " IS YOU"));
    }
    return SplashRenderer(splashes[static_cast<size_t>(random.next_int(count))]);
}

// `SplashRenderer.render`: the pulsing scale from the wall clock.
float SplashRenderer::scale_at(int64_t millis, int text_width) {
    const float turn = static_cast<float>(millis % 1000LL) / 1000.0f *
                       static_cast<float>(3.141592653589793 * 2.0);
    const float pulse = 1.8f - std::abs(jmath::sin(turn) * 0.1f);
    return pulse * 100.0f / static_cast<float>(text_width + 32);
}

// `SplashRenderer.render`: the splash centred on a point right of the logo, turned 20 degrees up and scaled to its width.
void SplashRenderer::render(GuiGraphics& gfx, int width, float alpha) const {
    const auto runs = gfx.runs(splash);
    const int text_width = gfx.font.width(runs);
    const float s = scale_at(gfx.millis, text_width);
    const float cx = static_cast<float>(width) / 2.0f + 123.0f;
    const float cy = 69.0f;
    const auto angle = static_cast<float>(-3.141592653589793 / 9.0);
    const auto sin = static_cast<float>(std::sin(static_cast<double>(angle)));
    const auto cos = static_cast<float>(std::cos(static_cast<double>(angle)));
    gfx.push_pose();
    gfx.translate(cx, cy);
    gfx.scale_by(s, s);
    gfx.quads.set_transform(cos, -sin, sin, cos, cx * static_cast<float>(gfx.scale),
                            cy * static_cast<float>(gfx.scale));
    gfx.draw_string(runs, -text_width / 2, -8, white(alpha));
    gfx.quads.reset_transform();
    gfx.pop_pose();
}

bool LogoRenderer::roll_easter_egg(jmath::LegacyRandom& random) {
    return random.next_float() < 1.0E-4f;
}

// `LogoRenderer.renderLogo`: the logo centred at the top and the edition under it, overlapping by seven.
void LogoRenderer::render_logo(GuiGraphics& gfx, int width, float alpha, int height_offset) const {
    const int x = width / 2 - 128;
    const uint32_t colour = white(keep_logo_through_fade ? 1.0f : alpha);
    gfx.blit(show_easter_egg ? "gui/title/minceraft" : "gui/title/minecraft", x, height_offset,
             0.0f, 0.0f, 256, 44, 256, 64, colour);
    const int edition_x = width / 2 - 64;
    const int edition_y = height_offset + 44 - 7;
    gfx.blit("gui/title/edition", edition_x, edition_y, 0.0f, 0.0f, 128, 14, 128, 16, colour);
}

SplashManager& shared_splashes() {
    static SplashManager manager = [] {
        SplashManager out;
        out.load(SplashManager::default_path());
        return out;
    }();
    return manager;
}

jmath::LegacyRandom& title_random() {
    static jmath::LegacyRandom random(unique_seed());
    return random;
}

// `TitleScreen.TitleScreen`: a logo of its own unless one is handed over.
TitleScreen::TitleScreen(bool fade, std::optional<LogoRenderer> given)
    : Screen(translatable("narrator.screen.title")), fading(fade),
      logo(given ? *given : [] {
          jmath::LegacyRandom random(unique_seed());
          return LogoRenderer(false, LogoRenderer::roll_easter_egg(random));
      }()) {}

// `TitleScreen.init`: the splash once, the three big buttons from a quarter down, the row of four under them, the copyright line.
void TitleScreen::init() {
    if (!splash) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        splash = shared_splashes().get_splash(local.tm_mon + 1, local.tm_mday,
                                             manager != nullptr ? manager->username : "",
                                             title_random());
    }
    Component copyright = translatable("title.credits");
    const int copyright_width =
        manager != nullptr && manager->font != nullptr ? manager->font->width(copyright) : 0;
    const int copyright_x = width - copyright_width - 2;
    constexpr int kSpacing = 24;
    int y = height / 4 + 48;
    y = create_normal_menu_options(y, kSpacing);
    Host* host = manager != nullptr ? &manager->host : nullptr;

    auto language = language_button(20, [](Button&) {}, true);
    y += 36;
    language->set_position(width / 2 - 124, y);
    widgets.push_back(std::move(language));
    add<Button>(width / 2 - 100, y, 98, 20, translatable("menu.options"), [](Button&) {});
    add<Button>(width / 2 + 2, y, 98, 20, translatable("menu.quit"), [host](Button&) {
        if (host != nullptr && host->quit) host->quit();
    });
    auto accessibility = accessibility_button(20, [](Button&) {}, true);
    accessibility->set_position(width / 2 + 104, y);
    widgets.push_back(std::move(accessibility));
    add<PlainTextButton>(copyright_x, height - 10, copyright_width, 10, std::move(copyright),
                         [](Button&) {});
}

// `TitleScreen.createNormalMenuOptions`: Singleplayer, Multiplayer and Realms, one under the other.
int TitleScreen::create_normal_menu_options(int y, int spacing) {
    add<Button>(width / 2 - 100, y, 200, 20, translatable("menu.singleplayer"), [](Button&) {})
        .active = false;
    Host* host = manager != nullptr ? &manager->host : nullptr;
    add<Button>(width / 2 - 100, y + spacing, 200, 20, translatable("menu.multiplayer"),
                [host](Button&) {
                    if (host != nullptr && host->multiplayer) host->multiplayer();
                });
    y = y + spacing + spacing;
    add<Button>(width / 2 - 100, y, 200, 20, translatable("menu.online"), [](Button&) {})
        .active = false;
    return y;
}

// `TitleScreen.render`: the fade over its second second, the panorama, the widgets, the logo, the splash and the version.
void TitleScreen::render(GuiGraphics& gfx, double mx, double my) {
    if (fade_in_start == 0 && fading) fade_in_start = gfx.millis;
    float alpha = 1.0f;
    if (fading) {
        float t = static_cast<float>(gfx.millis - fade_in_start) / 2000.0f;
        if (t > 1.0f) {
            fading = false;
        } else {
            t = std::clamp(t, 0.0f, 1.0f);
            alpha = std::clamp((t - 0.5f) / (1.0f - 0.5f), 0.0f, 1.0f);
        }
        fade_widgets(alpha);
    }
    render_panorama(gfx);
    Screen::render(gfx, mx, my);
    logo.render_logo(gfx, width, logo.keep_logo_through_fade ? 1.0f : alpha);
    const bool hide = manager != nullptr && manager->options != nullptr &&
                      manager->options->get_bool("hideSplashTexts");
    if (splash && !hide) splash->render(gfx, width, alpha);
    const std::string version = "Minecraft " + std::string(protocol::kTargetVersionName);
    gfx.draw_string(version, 2, height - 10, white(alpha));
}

} // namespace mcpp::ui
