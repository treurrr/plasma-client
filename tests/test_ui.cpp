#include "mcpp/byte_buffer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/json.hpp"
#include "mcpp/keybinds.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/options.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/server_list.hpp"
#include "mcpp/ui/text_component.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using namespace mcpp;
using namespace mcpp::ui;

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

const std::map<std::string, std::string, std::less<>>& language() {
    static const std::map<std::string, std::string, std::less<>> table = {
        {"chat.type.text", "<%s> %s"},
        {"death.attack.player", "%1$s was slain by %2$s"},
        {"multiplayer.player.joined", "%s joined the game"},
        {"options.generic_value", "%s: %s"},
        {"commands.percent", "100%% done"},
    };
    return table;
}

Resolve resolver() {
    Resolve r;
    r.translate = [](std::string_view key) -> const std::string* {
        const auto found = language().find(key);
        return found == language().end() ? nullptr : &found->second;
    };
    r.key_name = [](std::string_view key) { return key == "key.jump" ? std::string("Space") : std::string(key); };
    return r;
}

// Every glyph four wide but 'i', two: enough to tell bold from plain.
float advance(uint32_t code) { return code == 'i' ? 2.0f : 4.0f; }

void test_json_forms() {
    check(plain(from_json_text(R"("plain")"), resolver()) == "plain", "a bare string");
    check(plain(from_json_text("42"), resolver()) == "42", "a bare number");
    check(plain(from_json_text(R"(["a", {"text": "b"}, "c"])"), resolver()) == "abc",
          "a list is a parent and its siblings");
    const auto nested = from_json_text(
        R"({"text": "A", "color": "red", "bold": true,
            "extra": [{"text": "B"}, {"text": "C", "color": "#00ff00", "bold": false}]})");
    const auto runs = flatten(nested, resolver());
    check(runs.size() == 2, "same-styled pieces join into one run");
    check(runs.size() == 2 && runs[0].text == "AB" && runs[0].style.colour == 0xFF5555u &&
              runs[0].style.bold.value_or(false),
          "children inherit colour and bold");
    check(runs.size() == 2 && runs[1].text == "C" && runs[1].style.colour == 0x00FF00u &&
              !runs[1].style.bold.value_or(true),
          "a child overrides what it names");

    const auto death = from_json_text(
        R"({"translate": "death.attack.player", "color": "gray",
            "with": [{"text": "Steve", "color": "yellow"}, "Alex"]})");
    const auto death_runs = flatten(death, resolver());
    check(plain(death_runs) == "Steve was slain by Alex", "positional arguments");
    check(death_runs.size() == 2 && death_runs[0].style.colour == 0xFFFF55u &&
              death_runs[1].text == " was slain by Alex" &&
              death_runs[1].style.colour == 0xAAAAAAu,
          "an argument keeps its own colour, the pattern and plain arguments take the parent's");
    check(plain(from_json_text(R"({"translate": "commands.percent"})"), resolver()) ==
              "100% done",
          "%% is a percent sign");
    check(plain(from_json_text(R"({"translate": "no.such.key", "fallback": "Hi %s",
                                    "with": ["you"]})"),
                resolver()) == "Hi you",
          "a missing key falls back");
    check(plain(from_json_text(R"({"translate": "no.such.key"})"), resolver()) == "no.such.key",
          "a missing key with no fallback shows the key");
    check(plain(from_json_text(R"({"keybind": "key.jump"})"), resolver()) == "Space",
          "a keybind shows the key's name");
    check(plain(from_json_text(R"({"translate": "chat.type.text", "with": ["Bob", "hi"]})"),
                resolver()) == "<Bob> hi",
          "sequential arguments");

    const auto events = from_json_text(
        R"({"text": "x", "click_event": {"action": "open_url", "url": "https://a.b"},
            "hover_event": {"action": "show_text", "value": {"text": "tip"}}})");
    check(events.style.click_action == "open_url" && events.style.click_value == "https://a.b",
          "1.21.5 click events");
    check(events.style.hover_text && plain(*events.style.hover_text) == "tip",
          "1.21.5 hover events");
    const auto old = from_json_text(
        R"({"text": "x", "clickEvent": {"action": "run_command", "value": "/help"},
            "hoverEvent": {"action": "show_text", "contents": "old"}})");
    check(old.style.click_value == "/help" && old.style.hover_text &&
              plain(*old.style.hover_text) == "old",
          "the pre-1.21.5 event names");
    const auto shadow = from_json_text(R"({"text": "s", "shadow_color": [1.0, 0.0, 0.0, 0.5]})");
    check(shadow.style.shadow_colour == 0x80FF0000u, "shadow colour as four floats");
}

nbt::Value nbt_from(std::vector<uint8_t> bytes) {
    ByteBuffer in(std::move(bytes));
    return nbt::read_network_tag(in);
}

void put_name(std::vector<uint8_t>& out, std::string_view text) {
    out.push_back(static_cast<uint8_t>(text.size() >> 8));
    out.push_back(static_cast<uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

void test_nbt_forms() {
    // {text: "Hi", bold: 1b, extra: [{text: "!", italic: 1b}]}
    std::vector<uint8_t> tag = {10};
    tag.push_back(8);
    put_name(tag, "text");
    put_name(tag, "Hi");
    tag.push_back(1);
    put_name(tag, "bold");
    tag.push_back(1);
    tag.push_back(9);
    put_name(tag, "extra");
    tag.push_back(10);
    tag.insert(tag.end(), {0, 0, 0, 1});
    tag.push_back(8);
    put_name(tag, "text");
    put_name(tag, "!");
    tag.push_back(1);
    put_name(tag, "italic");
    tag.push_back(1);
    tag.push_back(0);
    tag.push_back(0);
    const auto runs = flatten(from_nbt(nbt_from(tag)), resolver());
    check(runs.size() == 2 && runs[0].text == "Hi" && runs[0].style.bold.value_or(false),
          "nbt: bytes are booleans");
    check(runs.size() == 2 && runs[1].style.italic.value_or(false) &&
              runs[1].style.bold.value_or(false),
          "nbt: a child inherits and adds");

    std::vector<uint8_t> bare = {8};
    put_name(bare, "just text");
    check(plain(from_nbt(nbt_from(bare))) == "just text", "nbt: a bare string tag");
}

void test_legacy_codes() {
    const auto runs = flatten_legacy("\xC2\xA7" "cRed \xC2\xA7lbold\xC2\xA7r plain");
    check(runs.size() == 3, "three runs from two codes and a reset");
    check(runs.size() == 3 && runs[0].style.colour == 0xFF5555u && runs[0].text == "Red ",
          "a colour code");
    check(runs.size() == 3 && runs[1].style.bold.value_or(false) &&
              runs[1].style.colour == 0xFF5555u,
          "bold keeps the colour");
    check(runs.size() == 3 && !runs[2].style.colour && !runs[2].style.bold.value_or(false),
          "reset goes back to the base style");
    const auto colour_resets = flatten_legacy("\xC2\xA7l" "a\xC2\xA7" "9b");
    check(colour_resets.size() == 2 && !colour_resets[1].style.bold.value_or(false),
          "a colour code clears bold");
}

void test_width_and_wrap() {
    Style bold;
    bold.bold = true;
    const std::vector<Run> runs = {{"ab", {}}, {"cd", bold}};
    check(width(runs, advance) == 4 + 4 + 5 + 5, "bold glyphs are one wider");
    const auto lines = wrap({{"one two three", {}}}, 30.0f, advance);
    check(lines.size() == 2 && plain(lines[0]) == "one two" && plain(lines[1]) == "three",
          "wraps at the last space that fits");
    const auto long_word = wrap({{"abcdefghij", {}}}, 16.0f, advance);
    check(long_word.size() == 3 && plain(long_word[0]) == "abcd", "a long word is cut");
    const auto newline = wrap({{"a\nb", {}}}, 100.0f, advance);
    check(newline.size() == 2 && plain(newline[1]) == "b", "a newline always breaks");
}

void test_options_round_trip() {
    Options options;
    check(options.serialise() == Options().serialise(), "defaults are stable");
    check(options.get_int("guiScale") == 0 && options.get_bool("enableVsync") &&
              options.get_int("maxFps") == 120 && options.get_int("renderDistance") == 12,
          "vanilla defaults");
    int key_lines = 0;
    for (const auto& [key, value] : options.entries()) {
        if (key.starts_with("key_")) ++key_lines;
    }
    check(key_lines == 59, std::format("59 key lines, got {}", key_lines));

    const std::string file = "version:4671\nguiScale:3\nfov:0.25\ngraphicsPreset:\"fast\"\n"
                             "someModOption:42\nnot a line\nlastServer:play.example.org\n";
    Options read;
    read.parse(file);
    check(read.get_int("guiScale") == 3 && read.get_double("fov") == 0.25 &&
              read.get_string("graphicsPreset") == "fast",
          "values read back");
    check(read.raw("someModOption") && *read.raw("someModOption") == "42", "unknown keys kept");
    const auto written = read.serialise();
    Options again;
    again.parse(written);
    check(again.serialise() == written, "round trip is exact");
    check(written.find("someModOption:42\n") != std::string::npos, "unknown keys written back");
    check(written.find("not a line") == std::string::npos, "malformed lines dropped");

    read.set_double("mouseSensitivity", 0.75);
    read.set_double("chatScale", 1.0);
    read.set_string("lang", "nl_nl");
    check(*read.raw("mouseSensitivity") == "0.75" && *read.raw("chatScale") == "1.0",
          "doubles written the way the game writes them");
    check(*read.raw("lang") == "\"nl_nl\"", "strings quoted");

    const fs::path dir = fs::temp_directory_path() / "plasma-test-options";
    fs::remove_all(dir);
    check(read.save(dir / "options.txt"), "options save");
    Options loaded;
    check(loaded.load(dir / "options.txt") && loaded.serialise() == read.serialise(),
          "options survive a save and a load");
    fs::remove_all(dir);

    if (const auto vanilla = Options::vanilla_path()) {
        Options real;
        check(real.load(*vanilla), "the launcher's options.txt reads");
        std::ifstream original(*vanilla, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(original)),
                               std::istreambuf_iterator<char>());
        size_t original_keys = 0;
        for (size_t at = 0; (at = text.find('\n', at)) != std::string::npos; ++at) ++original_keys;
        check(real.serialise() == text || real.entries().size() >= original_keys,
              "every line of the real file is kept");
        std::cout << std::format("  real options.txt: {} lines, guiScale {}\n", original_keys,
                                 real.get_int("guiScale"));
    }
}

void test_keybinds() {
    Keybinds binds;
    check(binds.all().size() == 59, "59 bindings");
    const auto* jump = binds.find("key.jump");
    check(jump && jump->current.name() == "key.keyboard.space", "jump is space");
    const auto* attack = binds.find("key.attack");
    check(attack && attack->current == InputKey::mouse(0), "attack is the left button");
    for (const auto name : {"key.keyboard.left.shift", "key.keyboard.f11", "key.keyboard.a",
                            "key.keyboard.7", "key.mouse.middle", "key.mouse.5",
                            "key.keyboard.keypad.enter", "key.keyboard.unknown"}) {
        const auto key = InputKey::from_name(name);
        check(key && key->name() == name, std::format("{} names itself", name));
    }
    Options options;
    options.set_raw("key_key.jump", "key.keyboard.j");
    binds.load(options);
    check(binds.find("key.jump")->current == InputKey::keyboard('J'), "a rebinding loads");
    Options out;
    binds.store(out);
    check(*out.raw("key_key.jump") == "key.keyboard.j", "and stores");
    binds.press(InputKey::keyboard('J'));
    check(binds.down("key.jump") && binds.consume("key.jump") && !binds.consume("key.jump"),
          "a press is held and consumed once");
    binds.release(InputKey::keyboard('J'));
    check(!binds.down("key.jump"), "released");
    Keybinds fresh;
    check(fresh.clashes(*fresh.find("key.left")).empty(),
          "a debug chord on the same letter is no clash");
    fresh.find("key.drop")->current = InputKey::keyboard('W');
    check(fresh.clashes(*fresh.find("key.forward")).size() == 1, "a real clash is found");
}

void test_server_list() {
    ServerList list;
    list.entries.push_back({"Home", "localhost:25565", "", ServerEntry::Packs::Prompt, false});
    list.entries.push_back({"Hub", "hub.example.org", "aWNvbg==", ServerEntry::Packs::Enabled, false});
    list.entries.push_back({"Minecraft Server", "direct.example", "", ServerEntry::Packs::Prompt, true});
    const auto bytes = list.encode();
    const auto back = ServerList::decode(bytes);
    check(back && back->entries.size() == 3, "servers.dat round trip");
    if (back && back->entries.size() == 3) {
        check(back->entries[1].icon == "aWNvbg==" &&
                  back->entries[1].packs == ServerEntry::Packs::Enabled,
              "icon and pack policy kept");
        check(back->entries[0].packs == ServerEntry::Packs::Prompt, "no acceptTextures is prompt");
        check(back->entries[2].hidden && back->visible().size() == 2,
              "the hidden direct-connect entry is kept and not listed");
        check(back->encode() == bytes, "encoding is stable");
    }
    const fs::path dir = fs::temp_directory_path() / "plasma-test-servers";
    fs::remove_all(dir);
    check(list.save(dir / "servers.dat"), "servers save");
    ServerList loaded;
    check(loaded.load(dir / "servers.dat") && loaded.encode() == bytes, "servers load");
    fs::remove_all(dir);

    if (const auto vanilla = ServerList::vanilla_path()) {
        ServerList real;
        check(real.load(*vanilla), "the launcher's servers.dat reads");
        size_t icons = 0;
        for (const auto& entry : real.entries) {
            if (entry.icon.empty()) continue;
            const auto png = read_png(base64_decode(entry.icon));
            check(png && png->width == 64 && png->height == 64, "its icon is a 64x64 png");
            ++icons;
        }
        std::cout << std::format("  real servers.dat: {} entries, {} visible, {} icons\n",
                                 real.entries.size(), real.visible().size(), icons);
    }
}

void test_base64_and_png() {
    const std::string text = "any carnal pleas";
    const std::vector<uint8_t> raw(text.begin(), text.end());
    check(base64_encode(raw) == "YW55IGNhcm5hbCBwbGVhcw==", "base64 encodes");
    check(base64_decode("YW55IGNhcm5hbCBwbGVhcw==") == raw, "base64 decodes");

    // A png written by this client reads back pixel for pixel.
    const fs::path path = fs::temp_directory_path() / "plasma-test-png.png";
    std::vector<uint8_t> pixels(5 * 3 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 7);
    write_png(path, 5, 3, pixels);
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    const auto image = read_png(bytes);
    check(image && image->width == 5 && image->height == 3 && image->rgba == pixels,
          "png round trip");
    fs::remove(path);
}

void test_gui_scale() {
    check(gui_scale(854, 480, 0, false) == 2, "854x480 auto is 2");
    check(gui_scale(1920, 1080, 0, false) == 4, "1080p auto is 4");
    check(gui_scale(3840, 2160, 0, false) == 9, "auto is not capped at 4");
    check(gui_scale(1920, 1080, 2, false) == 2, "the option caps it");
    check(gui_scale(1920, 1080, 3, true) == 4, "unicode wants an even scale");
    check(gui_extent(1601, 2) == 801, "a partial gui unit counts");
}

} // namespace

int main() {
    test_json_forms();
    test_nbt_forms();
    test_legacy_codes();
    test_width_and_wrap();
    test_options_round_trip();
    test_keybinds();
    test_server_list();
    test_base64_and_png();
    test_gui_scale();
    if (failures == 0) std::cout << "ui: all passed\n";
    return failures == 0 ? 0 : 1;
}
