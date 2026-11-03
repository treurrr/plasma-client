#include "mcpp/ui/hud/hud.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mcpp::ui::hud {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view ping_sprite(int32_t latency) {
    if (latency < 0) return "icon/ping_unknown";
    if (latency < 150) return "icon/ping_5";
    if (latency < 300) return "icon/ping_4";
    if (latency < 600) return "icon/ping_3";
    if (latency < 1000) return "icon/ping_2";
    return "icon/ping_1";
}

// Java's Float.toString for the half-heart counts the list shows: one
// decimal, which is all a value of n/2 ever needs.
std::string half_string(int value) {
    return std::format("{}.{}", value / 2, value % 2 == 0 ? 0 : 5);
}

} // namespace

void Gui::render_tab_list(GuiGraphics& gfx, const Options& options, const Frame& frame) {
    const Scoreboard* board = frame.scoreboard;
    const Scoreboard::Objective* objective =
        board != nullptr ? board->displayed(Scoreboard::kList) : nullptr;
    const std::string_view objective_name =
        objective != nullptr ? std::string_view(board->display[Scoreboard::kList]) : "";
    if (!frame.player_list_down || frame.players == nullptr) {
        if (tab_visible_) tab_health_.clear();
        tab_visible_ = false;
        return;
    }
    if (!tab_visible_) tab_health_.clear();
    tab_visible_ = true;

    std::vector<const TabEntry*> players;
    for (const auto& entry : *frame.players) players.push_back(&entry);
    const auto team_of = [&](const TabEntry& e) -> std::string {
        if (board == nullptr) return {};
        const auto found = board->team_of.find(e.name);
        return found == board->team_of.end() ? std::string{} : found->second;
    };
    std::stable_sort(players.begin(), players.end(), [&](const TabEntry* a, const TabEntry* b) {
        if (a->list_order != b->list_order) return a->list_order > b->list_order;
        const int sa = a->game_mode == 3 ? 1 : 0;
        const int sb = b->game_mode == 3 ? 1 : 0;
        if (sa != sb) return sa < sb;
        const auto ta = team_of(*a);
        const auto tb = team_of(*b);
        if (ta != tb) return ta < tb;
        return lower(a->name) < lower(b->name);
    });
    if (players.size() > 80) players.resize(80);

    struct Row {
        std::vector<Run> name;
        int score = 0;
        std::vector<Run> formatted;
        int score_width = 0;
    };
    std::vector<Row> rows;
    const int space = gfx.font.width(" ");
    int name_width = 0;
    int score_column = 0;
    for (const TabEntry* player : players) {
        Component name = player->has_display_name
                             ? player->display_name
                             : team_name(board != nullptr ? board->team_for(player->name) : nullptr,
                                         literal(player->name));
        if (player->game_mode == 3) {
            Component italic = literal("");
            italic.style.italic = true;
            italic.extra.push_back(std::move(name));
            name = std::move(italic);
        }
        Row row;
        row.name = gfx.runs(name);
        name_width = std::max(name_width, gfx.font.width(row.name));
        if (objective != nullptr) {
            const Scoreboard::Score* score = nullptr;
            if (const auto owner = board->scores.find(player->name); owner != board->scores.end()) {
                if (const auto found = owner->second.find(objective_name);
                    found != owner->second.end()) {
                    score = &found->second;
                }
            }
            if (score != nullptr) row.score = score->value;
            if (!objective->hearts) {
                row.formatted = gfx.runs(format_score(score, *objective, 0xFFFF55u));
                row.score_width = gfx.font.width(row.formatted);
                score_column = std::max(score_column, row.score_width > 0 ? space + row.score_width : 0);
            }
        }
        rows.push_back(std::move(row));
    }
    std::erase_if(tab_health_, [&](const auto& entry) {
        return std::none_of(players.begin(), players.end(),
                            [&](const TabEntry* p) { return p->uuid == entry.first; });
    });

    const int count = static_cast<int>(players.size());
    int per_column = count;
    int columns = 1;
    while (per_column > 20) {
        ++columns;
        per_column = (count + columns - 1) / columns;
    }
    const bool heads = frame.encrypted;
    const int score_width = objective == nullptr ? 0 : objective->hearts ? 90 : score_column;
    const int width = gfx.width;
    const int column_width =
        std::min(columns * ((heads ? 9 : 0) + name_width + score_width + 13), width - 50) / columns;
    const int left = width / 2 - (column_width * columns + (columns - 1) * 5) / 2;
    int y = 10;
    int box = column_width * columns + (columns - 1) * 5;
    std::vector<std::vector<Run>> header;
    std::vector<std::vector<Run>> footer;
    if (tab_header_) header = gfx.wrap(*tab_header_, width - 50);
    if (tab_footer_) footer = gfx.wrap(*tab_footer_, width - 50);
    for (const auto& line : header) box = std::max(box, gfx.font.width(line));
    for (const auto& line : footer) box = std::max(box, gfx.font.width(line));

    if (tab_header_) {
        gfx.fill(width / 2 - box / 2 - 1, y - 1, width / 2 + box / 2 + 1,
                 y + static_cast<int>(header.size()) * 9, 0x80000000u);
        for (const auto& line : header) {
            gfx.draw_string(line, width / 2 - gfx.font.width(line) / 2, y, 0xFFFFFFFFu, true);
            y += 9;
        }
        ++y;
    }
    gfx.fill(width / 2 - box / 2 - 1, y - 1, width / 2 + box / 2 + 1, y + per_column * 9,
             0x80000000u);
    const uint32_t cell = options.get_bool("backgroundForChatOnly")
                              ? 0x20FFFFFFu
                              : static_cast<uint32_t>(static_cast<int>(std::floor(
                                    options.get_double("textBackgroundOpacity") * 255.0)))
                                    << 24;
    for (int i = 0; i < count; ++i) {
        const int column = i / per_column;
        const int line = i % per_column;
        int x = left + column * column_width + column * 5;
        const int row_y = y + line * 9;
        gfx.fill(x, row_y, x + column_width, row_y + 8, cell);
        const TabEntry& player = *players[static_cast<size_t>(i)];
        const Row& row = rows[static_cast<size_t>(i)];
        if (heads) {
            if (player.skin_layer >= 0) {
                const float s = static_cast<float>(gfx.scale);
                gfx.quads.dynamic(static_cast<uint32_t>(player.skin_layer), static_cast<float>(x) * s,
                                  static_cast<float>(row_y) * s, 8.0f * s, 8.0f * s);
            }
            x += 9;
        }
        gfx.draw_string(row.name, x, row_y, player.game_mode == 3 ? 0x90FFFFFFu : 0xFFFFFFFFu,
                        true);
        if (objective != nullptr && player.game_mode != 3) {
            const int from = x + name_width + 1;
            const int to = from + score_width;
            if (to - from > 5) {
                if (objective->hearts) {
                    auto [found, added] = tab_health_.try_emplace(player.uuid);
                    auto& state = found->second;
                    if (added) state.last = state.displayed = row.score;
                    const int64_t now = tick_count_;
                    if (row.score != state.last) {
                        state.blink_until = now + (row.score < state.last ? 20 : 10);
                        state.last = row.score;
                        state.last_update = now;
                    }
                    if (now - state.last_update > 20) state.displayed = row.score;
                    const int filled = (std::max(row.score, state.displayed) + 1) / 2;
                    const int hearts = std::max(row.score, std::max(state.displayed, 20)) / 2;
                    const bool blink =
                        state.blink_until > now && (state.blink_until - now) % 6 >= 3;
                    if (filled > 0) {
                        const int step = static_cast<int>(std::floor(
                            std::min(static_cast<float>(to - from - 4) / static_cast<float>(hearts),
                                     9.0f)));
                        if (step <= 3) {
                            const float fraction = std::clamp(static_cast<float>(row.score) / 20.0f,
                                                              0.0f, 1.0f);
                            const uint32_t colour =
                                static_cast<uint32_t>(static_cast<int>((1.0f - fraction) * 255.0f))
                                    << 16 |
                                static_cast<uint32_t>(static_cast<int>(fraction * 255.0f)) << 8;
                            auto text = gfx.runs(translatable(
                                "multiplayer.player.list.hp", {literal(half_string(row.score))}));
                            if (to - gfx.font.width(text) < from) {
                                text = flatten_legacy(half_string(row.score));
                            }
                            gfx.draw_string(text, (to + from - gfx.font.width(text)) / 2, row_y,
                                            0xFF000000u | colour, true);
                        } else {
                            const std::string_view container =
                                blink ? "hud/heart/container_blinking" : "hud/heart/container";
                            for (int h = filled; h < hearts; ++h) {
                                gfx.blit_sprite(container, from + h * step, row_y, 9, 9);
                            }
                            for (int h = 0; h < filled; ++h) {
                                const int hx = from + h * step;
                                gfx.blit_sprite(container, hx, row_y, 9, 9);
                                if (blink) {
                                    if (h * 2 + 1 < state.displayed) {
                                        gfx.blit_sprite("hud/heart/full_blinking", hx, row_y, 9, 9);
                                    }
                                    if (h * 2 + 1 == state.displayed) {
                                        gfx.blit_sprite("hud/heart/half_blinking", hx, row_y, 9, 9);
                                    }
                                }
                                if (h * 2 + 1 < row.score) {
                                    gfx.blit_sprite(h >= 10 ? "hud/heart/absorbing_full_blinking"
                                                            : "hud/heart/full",
                                                    hx, row_y, 9, 9);
                                }
                                if (h * 2 + 1 == row.score) {
                                    gfx.blit_sprite(h >= 10 ? "hud/heart/absorbing_half_blinking"
                                                            : "hud/heart/half",
                                                    hx, row_y, 9, 9);
                                }
                            }
                        }
                    }
                } else if (!row.formatted.empty()) {
                    gfx.draw_string(row.formatted, to - row.score_width, row_y, 0xFFFFFFFFu, true);
                }
            }
        }
        gfx.blit_sprite(ping_sprite(player.latency), x - (heads ? 9 : 0) + column_width - 11, row_y,
                        10, 8);
    }
    if (tab_footer_) {
        y += per_column * 9 + 1;
        gfx.fill(width / 2 - box / 2 - 1, y - 1, width / 2 + box / 2 + 1,
                 y + static_cast<int>(footer.size()) * 9, 0x80000000u);
        for (const auto& line : footer) {
            gfx.draw_string(line, width / 2 - gfx.font.width(line) / 2, y, 0xFFFFFFFFu, true);
            y += 9;
        }
    }
}

} // namespace mcpp::ui::hud
