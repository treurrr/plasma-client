#include "mcpp/ui/options_ui.hpp"

#include <algorithm>
#include <cmath>
#include <format>

// The game's OptionInstance definitions, one entry per option: its name, its
// tooltip, the values it can take and the label each one shows. The value
// itself is read from and written to options.txt, in the form vanilla writes.
namespace mcpp::ui {
namespace {

Component tr(std::string key, std::vector<Component> args = {}) {
    return translatable(std::move(key), std::move(args));
}

std::vector<Component> one(Component text) { return {std::move(text)}; }

// `Mth.map`: the straight line from one range to the other.
double map_range(double value, double from_low, double from_high, double to_low, double to_high) {
    return to_low + (to_high - to_low) * ((value - from_low) / (from_high - from_low));
}

double as_double(const std::string& text) {
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        return 0.0;
    }
}

int as_int(const std::string& text) {
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        return 0;
    }
}

// What a value set does between the option's value and the slider's position.
struct IntRange {
    int min = 0;
    int max = 0;
    bool stepped = true;
    std::function<std::string(int)> encode = [](int v) { return std::to_string(v); };
    std::function<int(const std::string&)> decode = [](const std::string& t) { return as_int(t); };
};

// `OptionInstance.IntRangeBase.toSliderValue`, `fromSliderValue`.
double int_to_slider(const IntRange& range, int value) {
    if (value == range.min) return 0.0;
    if (value == range.max) return 1.0;
    return map_range(static_cast<double>(value) + 0.5, range.min, range.max + 1.0, 0.0, 1.0);
}

int int_from_slider(const IntRange& range, double position) {
    if (position >= 1.0) position = static_cast<double>(0.99999F);
    return static_cast<int>(
        std::floor(map_range(position, 0.0, 1.0, range.min, range.max + 1.0)));
}

OptionDef int_slider(std::string key, std::string caption_key, IntRange range,
                     std::function<Component(const Component&, int)> label,
                     std::vector<Component> tip = {}, bool immediate = true) {
    OptionDef def;
    def.key = std::move(key);
    def.caption = tr(caption_key);
    def.kind = OptionDef::Kind::Slider;
    def.apply_immediately = immediate;
    def.label = [label, range](const Component& caption, const std::string& stored) {
        return label(caption, range.decode(stored));
    };
    if (!tip.empty()) {
        def.tooltip = [tip](const std::string&) { return tip; };
    }
    def.to_slider = [range](const std::string& stored) {
        return int_to_slider(range, range.decode(stored));
    };
    def.from_slider = [range](double position) {
        return range.encode(int_from_slider(range, position));
    };
    if (range.stepped) {
        def.step = [range](const std::string& stored,
                           int direction) -> std::optional<std::string> {
            const int next = range.decode(stored) + direction;
            if (next < range.min || next > range.max) return std::nullopt;
            return range.encode(next);
        };
    }
    return def;
}

// `OptionInstance.UnitDouble.fromSliderValue`: a double from 0 to 1 that is
// its own slider position, with a mapping for the squared ones.
OptionDef unit_slider(std::string key, std::string caption_key,
                      std::function<Component(const Component&, double)> label,
                      std::vector<Component> tip = {},
                      std::function<double(double)> from_position = {},
                      std::function<double(double)> to_position = {}) {
    OptionDef def;
    def.key = std::move(key);
    def.caption = tr(caption_key);
    def.kind = OptionDef::Kind::Slider;
    def.label = [label](const Component& caption, const std::string& stored) {
        return label(caption, as_double(stored));
    };
    if (!tip.empty()) {
        def.tooltip = [tip](const std::string&) { return tip; };
    }
    def.to_slider = [to_position](const std::string& stored) {
        const double value = as_double(stored);
        return std::clamp(to_position ? to_position(value) : value, 0.0, 1.0);
    };
    def.from_slider = [from_position](double position) {
        return format_double(from_position ? from_position(position) : position);
    };
    return def;
}

OptionDef boolean_def(std::string key, std::string caption_key, std::vector<Component> tip = {}) {
    OptionDef def;
    def.key = std::move(key);
    def.caption = tr(caption_key);
    def.kind = OptionDef::Kind::Cycle;
    // `OptionInstance.createBoolean`: true first, false second.
    def.values = {"true", "false"};
    def.label = [](const Component&, const std::string& stored) {
        return tr(stored == "true" ? "options.on" : "options.off");
    };
    if (!tip.empty()) {
        def.tooltip = [tip](const std::string&) { return tip; };
    }
    return def;
}

// A boolean whose two values have names of their own: Toggle/Hold, the chat
// background, the text direction.
OptionDef boolean_named(std::string key, std::string caption_key, std::string when_true,
                        std::string when_false, std::vector<Component> tip = {}) {
    OptionDef def = boolean_def(std::move(key), std::move(caption_key), std::move(tip));
    def.label = [when_true, when_false](const Component&, const std::string& stored) {
        return tr(stored == "true" ? when_true : when_false);
    };
    return def;
}

// An enum options.txt holds as its ordinal.
OptionDef enum_int(std::string key, std::string caption_key,
                   std::vector<std::string> value_keys,
                   std::vector<std::string> value_tooltips = {}) {
    OptionDef def;
    def.key = std::move(key);
    def.caption = tr(caption_key);
    def.kind = OptionDef::Kind::Cycle;
    for (size_t i = 0; i < value_keys.size(); ++i) def.values.push_back(std::to_string(i));
    def.label = [value_keys](const Component&, const std::string& stored) {
        const auto index = static_cast<size_t>(std::max(0, as_int(stored)));
        return tr(value_keys[std::min(index, value_keys.size() - 1)]);
    };
    if (!value_tooltips.empty()) {
        def.tooltip = [value_tooltips](const std::string& stored) {
            const auto index = static_cast<size_t>(std::max(0, as_int(stored)));
            return one(tr(value_tooltips[std::min(index, value_tooltips.size() - 1)]));
        };
    }
    return def;
}

// An enum options.txt holds as its own quoted name.
OptionDef enum_string(std::string key, std::string caption_key,
                      std::vector<std::pair<std::string, std::string>> names,
                      std::vector<std::string> value_tooltips = {}) {
    OptionDef def;
    def.key = std::move(key);
    def.caption = tr(caption_key);
    def.kind = OptionDef::Kind::Cycle;
    def.quoted = true;
    for (const auto& [name, label] : names) def.values.push_back(name);
    def.label = [names](const Component&, const std::string& stored) {
        for (const auto& [name, label] : names) {
            if (name == stored) return tr(label);
        }
        return tr(names.front().second);
    };
    if (!value_tooltips.empty()) {
        def.tooltip = [names, value_tooltips](const std::string& stored) {
            for (size_t i = 0; i < names.size() && i < value_tooltips.size(); ++i) {
                if (names[i].first == stored) return one(tr(value_tooltips[i]));
            }
            return std::vector<Component>{};
        };
    }
    return def;
}

// `Options.logMouse`, `Options.unlogMouse`: the wheel's curve.
double log_mouse(int value) { return std::pow(10.0, static_cast<double>(value) / 100.0); }
// `Options.unlogMouse`: the wheel's value back to its slider step.
int unlog_mouse(double value) {
    return static_cast<int>(std::floor(std::log10(value) * 100.0));
}

std::vector<OptionDef> build() {
    std::vector<OptionDef> defs;
    const auto add = [&defs](OptionDef def) { defs.push_back(std::move(def)); };

    // Video: display.
    add(int_slider("maxFps", "options.framerateLimit",
                   {1, 26, true, [](int v) { return std::to_string(v * 10); },
                    [](const std::string& t) { return as_int(t) / 10; }},
                   [](const Component& caption, int value) {
                       return value == 260
                                  ? generic_value_label(caption,
                                                        tr("options.framerateLimit.max"))
                                  : generic_value_label(
                                        caption, tr("options.framerate", {literal(std::to_string(value))}));
                   }));
    add(boolean_def("enableVsync", "options.vsync"));
    add(enum_string("inactivityFpsLimit", "options.inactivityFpsLimit",
                    {{"minimized", "options.inactivityFpsLimit.minimized"},
                     {"afk", "options.inactivityFpsLimit.afk"}},
                    {"options.inactivityFpsLimit.minimized.tooltip",
                     "options.inactivityFpsLimit.afk.tooltip"}));
    {
        // `Options.guiScale`: a cycle from auto up to the largest whole scale
        // the window allows.
        OptionDef def;
        def.key = "guiScale";
        def.caption = tr("options.guiScale");
        def.kind = OptionDef::Kind::Cycle;
        for (int scale = 0; scale <= 4; ++scale) def.values.push_back(std::to_string(scale));
        def.label = [](const Component&, const std::string& stored) {
            return stored == "0" ? tr("options.guiScale.auto") : literal(stored);
        };
        add(std::move(def));
    }
    add(boolean_def("fullscreen", "options.fullscreen"));
    add(unit_slider("gamma", "options.gamma", [](const Component& caption, double value) {
        const int percent = static_cast<int>(value * 100.0);
        if (percent == 0) return generic_value_label(caption, tr("options.gamma.min"));
        if (percent == 50) return generic_value_label(caption, tr("options.gamma.default"));
        if (percent == 100) return generic_value_label(caption, tr("options.gamma.max"));
        return generic_value_label(caption, percent);
    }));

    // Video: quality.
    {
        // `Options.graphicsPreset`: a slider over the four presets.
        OptionDef def;
        def.key = "graphicsPreset";
        def.caption = tr("options.graphics.preset");
        def.kind = OptionDef::Kind::Slider;
        def.quoted = true;
        const std::vector<std::pair<std::string, std::string>> presets = {
            {"fast", "options.graphics.fast"},
            {"fancy", "options.graphics.fancy"},
            {"fabulous", "options.graphics.fabulous"},
            {"custom", "options.graphics.custom"}};
        def.values = {"fast", "fancy", "fabulous", "custom"};
        def.tooltip = [](const std::string&) { return one(tr("options.graphics.preset.tooltip")); };
        def.label = [presets](const Component& caption, const std::string& stored) {
            for (const auto& [name, label] : presets) {
                if (name == stored) return generic_value_label(caption, tr(label));
            }
            return generic_value_label(caption, tr(presets[1].second));
        };
        // `OptionInstance.SliderableEnum.toSliderValue`: the first value at 0, the last at 1.
        def.to_slider = [presets](const std::string& stored) {
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].first != stored) continue;
                if (i == 0) return 0.0;
                if (i + 1 == presets.size()) return 1.0;
                return map_range(static_cast<double>(i), 0.0,
                                 static_cast<double>(presets.size() - 1), 0.0, 1.0);
            }
            return 0.0;
        };
        def.from_slider = [presets](double position) {
            const auto index = static_cast<size_t>(
                std::floor(position * static_cast<double>(presets.size() - 1) + 0.5));
            return presets[std::min(index, presets.size() - 1)].first;
        };
        def.step = [presets](const std::string& stored,
                             int direction) -> std::optional<std::string> {
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].first != stored) continue;
                const int next = static_cast<int>(i) + direction;
                if (next < 0 || next >= static_cast<int>(presets.size())) return std::nullopt;
                return presets[static_cast<size_t>(next)].first;
            }
            return std::nullopt;
        };
        add(std::move(def));
    }
    add(int_slider("biomeBlendRadius", "options.biomeBlendRadius", {0, 7, false},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.biomeBlendRadius." + std::to_string(value * 2 + 1)));
                   },
                   {}, false));
    add(int_slider("renderDistance", "options.renderDistance", {2, 32, false},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.chunks", {literal(std::to_string(value))}));
                   },
                   {}, false));
    add(enum_int("prioritizeChunkUpdates", "options.prioritizeChunkUpdates",
                 {"options.prioritizeChunkUpdates.none", "options.prioritizeChunkUpdates.byPlayer",
                  "options.prioritizeChunkUpdates.nearby"},
                 {"options.prioritizeChunkUpdates.none.tooltip",
                  "options.prioritizeChunkUpdates.byPlayer.tooltip",
                  "options.prioritizeChunkUpdates.nearby.tooltip"}));
    add(int_slider("simulationDistance", "options.simulationDistance", {5, 32, false},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.chunks", {literal(std::to_string(value))}));
                   },
                   {}, false));
    add(boolean_def("ao", "options.ao"));
    add(enum_string("renderClouds", "options.renderClouds",
                    {{"false", "options.off"},
                     {"fast", "options.clouds.fast"},
                     {"true", "options.clouds.fancy"}}));
    add(enum_int("particles", "options.particles",
                 {"options.particles.all", "options.particles.decreased",
                  "options.particles.minimal"}));
    add(int_slider("mipmapLevels", "options.mipmapLevels", {0, 4, true},
                   [](const Component& caption, int value) {
                       return value == 0 ? option_status(caption, false)
                                         : generic_value_label(caption, value);
                   }));
    add(boolean_def("entityShadows", "options.entityShadows"));
    add(int_slider("entityDistanceScaling", "options.entityDistanceScaling",
                   {2, 20, true,
                    [](int v) { return format_double(static_cast<double>(v) / 4.0); },
                    [](const std::string& t) { return static_cast<int>(as_double(t) * 4.0); }},
                   [](const Component& caption, int value) {
                       return percent_value_label(caption, static_cast<double>(value) / 4.0);
                   }));
    add(int_slider("menuBackgroundBlurriness", "options.accessibility.menu_background_blurriness",
                   {0, 10, true}, &generic_value_or_off_label,
                   one(tr("options.accessibility.menu_background_blurriness.tooltip"))));
    add(int_slider("cloudRange", "options.renderCloudsDistance", {2, 128, true},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.chunks", {literal(std::to_string(value))}));
                   }));
    add(boolean_def("cutoutLeaves", "options.cutoutLeaves", one(tr("options.cutoutLeaves.tooltip"))));
    add(boolean_def("improvedTransparency", "options.improvedTransparency",
                    one(tr("options.improvedTransparency.tooltip"))));
    add(enum_int("textureFiltering", "options.textureFiltering",
                 {"options.textureFiltering.none", "options.textureFiltering.rgss",
                  "options.textureFiltering.anisotropic"},
                 {"options.textureFiltering.none.tooltip", "options.textureFiltering.rgss.tooltip",
                  "options.textureFiltering.anisotropic.tooltip"}));
    add(int_slider("maxAnisotropyBit", "options.maxAnisotropy", {1, 3, true},
                   [](const Component& caption, int value) {
                       return value == 0
                                  ? option_status(caption, false)
                                  : generic_value_label(
                                        caption,
                                        tr("options.multiplier",
                                           {literal(std::to_string(1 << value))}));
                   },
                   one(tr("options.maxAnisotropy.tooltip"))));
    add(int_slider("weatherRadius", "options.weatherRadius", {3, 10, true},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.blocks", {literal(std::to_string(value))}));
                   },
                   one(tr("options.weatherRadius.tooltip"))));

    // Video: preferences.
    add(boolean_def("showAutosaveIndicator", "options.autosaveIndicator"));
    add(boolean_def("vignette", "options.vignette", one(tr("options.vignette.tooltip"))));
    add(enum_int("attackIndicator", "options.attackIndicator",
                 {"options.off", "options.attack.crosshair", "options.attack.hotbar"}));
    add(int_slider("chunkSectionFadeInTime", "options.chunkFade",
                   {0, 40, true,
                    [](int v) { return format_double(static_cast<double>(v) / 20.0); },
                    [](const std::string& t) { return static_cast<int>(as_double(t) * 20.0); }},
                   [](const Component& caption, int value) {
                       const double seconds = static_cast<double>(value) / 20.0;
                       (void)caption;
                       return seconds <= 0.0
                                  ? tr("options.chunkFade.none")
                                  : tr("options.chunkFade.seconds",
                                       {literal(std::format("{:.2f}", seconds))});
                   },
                   one(tr("options.chunkFade.tooltip"))));

    // Controls.
    add(boolean_named("toggleCrouch", "key.sneak", "options.key.toggle", "options.key.hold"));
    add(boolean_named("toggleSprint", "key.sprint", "options.key.toggle", "options.key.hold"));
    add(boolean_named("toggleAttack", "key.attack", "options.key.toggle", "options.key.hold"));
    add(boolean_named("toggleUse", "key.use", "options.key.toggle", "options.key.hold"));
    add(boolean_def("autoJump", "options.autoJump"));
    add(int_slider("sprintWindow", "options.sprintWindow", {0, 10, true},
                   [](const Component& caption, int value) {
                       return value == 0 ? generic_value_label(caption, tr("options.off"))
                                         : generic_value_label(
                                               caption, tr("options.value",
                                                           {literal(std::to_string(value))}));
                   },
                   one(tr("options.sprintWindow.tooltip"))));
    add(boolean_def("operatorItemsTab", "options.operatorItemsTab"));

    // Mouse.
    add(unit_slider("mouseSensitivity", "options.sensitivity",
                    [](const Component& caption, double value) {
                        if (value == 0.0) {
                            return generic_value_label(caption, tr("options.sensitivity.min"));
                        }
                        if (value == 1.0) {
                            return generic_value_label(caption, tr("options.sensitivity.max"));
                        }
                        return percent_value_label(caption, 2.0 * value);
                    }));
    add(boolean_def("touchscreen", "options.touchscreen"));
    add(int_slider("mouseWheelSensitivity", "options.mouseWheelSensitivity",
                   {-200, 100, false, [](int v) { return format_double(log_mouse(v)); },
                    [](const std::string& t) { return unlog_mouse(as_double(t)); }},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, literal(std::format("{:.2f}", log_mouse(value))));
                   },
                   {}, false));
    add(boolean_def("discrete_mouse_scroll", "options.discrete_mouse_scroll"));
    add(boolean_def("invertXMouse", "options.invertMouseX"));
    add(boolean_def("invertYMouse", "options.invertMouseY"));
    add(boolean_def("allowCursorChanges", "options.allowCursorChanges",
                    one(tr("options.allowCursorChanges.tooltip"))));
    add(boolean_def("rawMouseInput", "options.rawMouseInput"));

    // Chat.
    add(enum_int("chatVisibility", "options.chat.visibility",
                 {"options.chat.visibility.full", "options.chat.visibility.system",
                  "options.chat.visibility.hidden"}));
    add(boolean_def("chatColors", "options.chat.color"));
    add(boolean_def("chatLinks", "options.chat.links"));
    add(boolean_def("chatLinksPrompt", "options.chat.links.prompt"));
    add(unit_slider("chatOpacity", "options.chat.opacity",
                    [](const Component& caption, double value) {
                        return percent_value_label(caption, value * 0.9 + 0.1);
                    }));
    add(unit_slider("textBackgroundOpacity", "options.accessibility.text_background_opacity",
                    &percent_value_label));
    add(unit_slider("chatScale", "options.chat.scale",
                    [](const Component& caption, double value) {
                        return value == 0.0 ? option_status(caption, false)
                                            : percent_value_label(caption, value);
                    }));
    add(unit_slider("chatLineSpacing", "options.chat.line_spacing", &percent_value_label));
    add(int_slider("chatDelay", "options.chat.delay_instant",
                   {0, 60, true,
                    [](int v) { return format_double(static_cast<double>(v) / 10.0); },
                    [](const std::string& t) { return static_cast<int>(as_double(t) * 10.0); }},
                   [](const Component& caption, int value) {
                       const double seconds = static_cast<double>(value) / 10.0;
                       (void)caption;
                       return seconds <= 0.0
                                  ? tr("options.chat.delay_none")
                                  : tr("options.chat.delay",
                                       {literal(std::format("{:.1f}", seconds))});
                   }));
    add(unit_slider("chatWidth", "options.chat.width",
                    [](const Component& caption, double value) {
                        return pixel_value_label(caption, chat_width_px(value));
                    }));
    add(unit_slider("chatHeightFocused", "options.chat.height.focused",
                    [](const Component& caption, double value) {
                        return pixel_value_label(caption, chat_height_px(value));
                    }));
    add(unit_slider("chatHeightUnfocused", "options.chat.height.unfocused",
                    [](const Component& caption, double value) {
                        return pixel_value_label(caption, chat_height_px(value));
                    }));
    add(enum_int("narrator", "options.narrator",
                 {"options.narrator.off", "options.narrator.all", "options.narrator.chat",
                  "options.narrator.system"}));
    add(boolean_def("autoSuggestions", "options.autoSuggestCommands"));
    add(boolean_def("hideMatchedNames", "options.hideMatchedNames",
                    one(tr("options.hideMatchedNames.tooltip"))));
    add(boolean_def("reducedDebugInfo", "options.reducedDebugInfo"));
    add(boolean_def("onlyShowSecureChat", "options.onlyShowSecureChat",
                    one(tr("options.onlyShowSecureChat.tooltip"))));
    add(boolean_def("saveChatDrafts", "options.chat.drafts",
                    one(tr("options.chat.drafts.tooltip"))));

    // Sound and music.
    for (const char* source : {"master", "music", "record", "weather", "block", "hostile",
                               "neutral", "player", "ambient", "voice", "ui"}) {
        add(unit_slider(std::string("soundCategory_") + source,
                        std::string("soundCategory.") + source, &percent_value_or_off_label));
    }
    {
        // `Options.soundDevice`: the one device this client knows is the
        // system's own.
        OptionDef def;
        def.key = "soundDevice";
        def.caption = tr("options.audioDevice");
        def.kind = OptionDef::Kind::Cycle;
        def.quoted = true;
        def.values = {""};
        def.label = [](const Component&, const std::string& stored) {
            return stored.empty() ? tr("options.audioDevice.default") : literal(stored);
        };
        add(std::move(def));
    }
    add(boolean_def("showSubtitles", "options.showSubtitles",
                    one(tr("options.showSubtitles.tooltip"))));
    {
        OptionDef def = boolean_def("directionalAudio", "options.directionalAudio");
        def.tooltip = [](const std::string& stored) {
            return one(tr(stored == "true" ? "options.directionalAudio.on.tooltip"
                                           : "options.directionalAudio.off.tooltip"));
        };
        add(std::move(def));
    }
    add(enum_string("musicFrequency", "options.music_frequency",
                    {{"DEFAULT", "options.music_frequency.default"},
                     {"FREQUENT", "options.music_frequency.frequent"},
                     {"CONSTANT", "options.music_frequency.constant"}},
                    {"options.music_frequency.tooltip", "options.music_frequency.tooltip",
                     "options.music_frequency.tooltip"}));
    add(enum_string("musicToast", "options.musicToast",
                    {{"never", "options.musicToast.never"},
                     {"pause", "options.musicToast.pauseMenu"},
                     {"pause_and_toast", "options.musicToast.pauseMenuAndToast"}},
                    {"options.musicToast.never.tooltip", "options.musicToast.pauseMenu.tooltip",
                     "options.musicToast.pauseMenuAndToast.tooltip"}));

    // Skin customization.
    for (const char* part : {"cape", "jacket", "left_sleeve", "right_sleeve", "left_pants_leg",
                             "right_pants_leg", "hat"}) {
        add(boolean_def(std::string("modelPart_") + part,
                        std::string("options.modelPart.") + part));
    }
    add(enum_string("mainHand", "options.mainHand",
                    {{"left", "options.mainHand.left"}, {"right", "options.mainHand.right"}}));

    // Accessibility.
    add(boolean_def("highContrast", "options.accessibility.high_contrast",
                    one(tr("options.accessibility.high_contrast.tooltip"))));
    add(boolean_named("backgroundForChatOnly", "options.accessibility.text_background",
                      "options.accessibility.text_background.chat",
                      "options.accessibility.text_background.everywhere"));
    add(int_slider("notificationDisplayTime", "options.notifications.display_time",
                   {5, 100, true,
                    [](int v) { return format_double(static_cast<double>(v) / 10.0); },
                    [](const std::string& t) { return static_cast<int>(as_double(t) * 10.0); }},
                   [](const Component& caption, int value) {
                       return generic_value_label(
                           caption, tr("options.multiplier",
                                       {literal(format_double(static_cast<double>(value) / 10.0))}));
                   },
                   one(tr("options.notifications.display_time.tooltip"))));
    add(boolean_def("bobView", "options.viewBobbing"));
    add(unit_slider("screenEffectScale", "options.screenEffectScale", &percent_value_or_off_label,
                    one(tr("options.screenEffectScale.tooltip"))));
    add(unit_slider("fovEffectScale", "options.fovEffectScale", &percent_value_or_off_label,
                    one(tr("options.fovEffectScale.tooltip")),
                    [](double position) { return position * position; },
                    [](double value) { return std::sqrt(value); }));
    add(unit_slider("darknessEffectScale", "options.darknessEffectScale",
                    &percent_value_or_off_label, one(tr("options.darknessEffectScale.tooltip")),
                    [](double position) { return position * position; },
                    [](double value) { return std::sqrt(value); }));
    add(unit_slider("damageTiltStrength", "options.damageTiltStrength",
                    &percent_value_or_off_label, one(tr("options.damageTiltStrength.tooltip"))));
    add(unit_slider("glintSpeed", "options.glintSpeed", &percent_value_or_off_label,
                    one(tr("options.glintSpeed.tooltip"))));
    add(unit_slider("glintStrength", "options.glintStrength", &percent_value_or_off_label,
                    one(tr("options.glintStrength.tooltip"))));
    add(boolean_def("hideLightningFlashes", "options.hideLightningFlashes",
                    one(tr("options.hideLightningFlashes.tooltip"))));
    add(boolean_def("darkMojangStudiosBackground", "options.darkMojangStudiosBackgroundColor",
                    one(tr("options.darkMojangStudiosBackgroundColor.tooltip"))));
    add(unit_slider("panoramaScrollSpeed", "options.accessibility.panorama_speed",
                    &percent_value_label));
    add(boolean_def("hideSplashTexts", "options.hideSplashTexts",
                    one(tr("options.hideSplashTexts.tooltip"))));
    add(boolean_def("narratorHotkey", "options.accessibility.narrator_hotkey",
                    one(tr("options.accessibility.narrator_hotkey.tooltip"))));
    add(boolean_def("rotateWithMinecart", "options.rotateWithMinecart",
                    one(tr("options.rotateWithMinecart.tooltip"))));
    add(boolean_def("highContrastBlockOutline", "options.accessibility.high_contrast_block_outline",
                    one(tr("options.accessibility.high_contrast_block_outline.tooltip"))));

    // Font.
    add(boolean_def("forceUnicodeFont", "options.forceUnicodeFont"));
    add(boolean_def("japaneseGlyphVariants", "options.japaneseGlyphVariants",
                    one(tr("options.japaneseGlyphVariants.tooltip"))));

    // Online.
    add(boolean_def("realmsNotifications", "options.realmsNotifications",
                    one(tr("options.realmsNotifications.tooltip"))));
    add(boolean_def("allowServerListing", "options.allowServerListing",
                    one(tr("options.allowServerListing.tooltip"))));

    // The field of view, which the options screen itself shows.
    add(int_slider("fov", "options.fov",
                   {30, 110, true,
                    [](int v) { return format_double((static_cast<double>(v) - 70.0) / 40.0); },
                    [](const std::string& t) {
                        return static_cast<int>(as_double(t) * 40.0 + 70.0);
                    }},
                   [](const Component& caption, int value) {
                       if (value == 70) return generic_value_label(caption, tr("options.fov.min"));
                       if (value == 110) return generic_value_label(caption, tr("options.fov.max"));
                       return generic_value_label(caption, value);
                   }));
    return defs;
}

} // namespace

// `Options.genericValueLabel`: "Name: value".
Component generic_value_label(const Component& caption, Component value) {
    return translatable("options.generic_value", {caption, std::move(value)});
}

Component generic_value_label(const Component& caption, int value) {
    return generic_value_label(caption, literal(std::to_string(value)));
}

// `Options.genericValueOrOffLabel`: zero reads OFF.
Component generic_value_or_off_label(const Component& caption, int value) {
    return value == 0 ? generic_value_label(caption, tr("options.off"))
                      : generic_value_label(caption, value);
}

// `Options.percentValueLabel`: the value as whole percent.
Component percent_value_label(const Component& caption, double value) {
    return translatable(
        "options.percent_value",
        {caption, literal(std::to_string(static_cast<int>(value * 100.0)))});
}

// `Options.percentValueOrOffLabel`: zero reads OFF.
Component percent_value_or_off_label(const Component& caption, double value) {
    return value == 0.0 ? generic_value_label(caption, tr("options.off"))
                        : percent_value_label(caption, value);
}

// `Options.pixelValueLabel`: the value in pixels.
Component pixel_value_label(const Component& caption, int value) {
    return translatable("options.pixel_value", {caption, literal(std::to_string(value))});
}

// `CommonComponents.optionStatus`: the composed on and off lines.
Component option_status(const Component& caption, bool on) {
    return translatable(on ? "options.on.composed" : "options.off.composed", {caption});
}

// `ChatComponent.getWidth`, `ChatComponent.getHeight`.
int chat_width_px(double fraction) {
    return static_cast<int>(std::floor(fraction * 280.0 + 40.0));
}

int chat_height_px(double fraction) {
    return static_cast<int>(std::floor(fraction * 160.0 + 20.0));
}

const std::vector<OptionDef>& option_defs() {
    static const std::vector<OptionDef> defs = build();
    return defs;
}

const OptionDef* option_def(std::string_view key) {
    for (const auto& def : option_defs()) {
        if (def.key == key) return &def;
    }
    return nullptr;
}

std::string OptionDef::get(const Options& options) const {
    if (quoted) return options.get_string(key);
    if (const auto* value = options.raw(key)) return *value;
    if (const auto* fallback = Options::default_of(key)) return *fallback;
    return {};
}

void OptionDef::set(Options& options, const std::string& value) const {
    if (quoted) {
        options.set_string(key, value);
    } else {
        options.set_raw(key, value);
    }
}

Component OptionDef::message(const Options& options) const {
    const std::string value = get(options);
    const Component inner = label ? label(caption, value) : literal(value);
    // `CycleButton.createFullName`: the cycle button composes the name with
    // the value; a slider's label already is the whole line.
    return kind == Kind::Cycle ? generic_value_label(caption, inner) : inner;
}

} // namespace mcpp::ui
