#include "mcpp/protocol_versions.hpp"
#include "mcpp/ui/chat_screen.hpp"
#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/multiplayer.hpp"
#include "mcpp/ui/options_ui.hpp"
#include "mcpp/ui/pause_screen.hpp"
#include "mcpp/ui/screens.hpp"
#include "mcpp/ui/title_screen.hpp"

#include <filesystem>

namespace mcpp::ui {
namespace {

// F: the settings and the bindings a screen made by name works on. They are
// the game's own defaults, so a render of a screen is the same every time;
// nothing here is ever written to the player's own files.
Options& shown_options() {
    static Options options;
    return options;
}

Keybinds& shown_keybinds() {
    static Keybinds binds = [] {
        Keybinds made;
        made.load(shown_options());
        return made;
    }();
    return binds;
}

// F: a server list that holds three entries in three states, so the row with
// its icon, its ping bars and its version can be looked at without a server.
class ShownMultiplayerScreen : public JoinMultiplayerScreen {
public:
    ShownMultiplayerScreen() : JoinMultiplayerScreen(nullptr) {
        file = std::filesystem::temp_directory_path() / "plasma-shown-servers.dat";
        ServerEntry good;
        good.name = "BradgeSMP";
        good.ip = "mc.bradgesmp.nl";
        good.state = ServerEntry::State::Successful;
        good.ping = 24;
        good.version = protocol::kTargetVersionName;
        good.protocol = protocol::kTargetProtocol;
        good.players_online = 7;
        good.players_max = 40;
        good.motd = "Survival, duels and tiers\nJoin the fight";
        good.player_list = {"treur", "220t", "kinate"};
        ServerEntry old;
        old.name = "Old Server";
        old.ip = "old.example.com";
        old.state = ServerEntry::State::Incompatible;
        old.ping = 310;
        old.version = "1.20.1";
        old.protocol = 763;
        old.motd = "A server of an older version";
        ServerEntry down;
        down.name = "Away";
        down.ip = "away.example.com";
        down.state = ServerEntry::State::Unreachable;
        down.motd = "Can't connect to server";
        servers.entries = {good, old, down};
    }
};

// F: a chat screen with a few lines behind it, one of them with a hover and a
// click event, so what the screen draws over can be looked at.
class ShownChatScreen : public ChatScreen {
public:
    ShownChatScreen() : ChatScreen("/gamemode ", false) {
        chat = &lines_;
        commands = &tree_;
        tree_.nodes.resize(6);
        tree_.root = 0;
        tree_.nodes[0].children = {1, 2};
        for (size_t i = 1; i < tree_.nodes.size(); ++i) {
            tree_.nodes[i].kind = proto::CommandNode::Kind::Literal;
        }
        tree_.nodes[1].name = "gamemode";
        tree_.nodes[1].children = {3, 4, 5};
        tree_.nodes[2].name = "gamerule";
        tree_.nodes[3].name = "creative";
        tree_.nodes[4].name = "spectator";
        tree_.nodes[5].name = "survival";
    }

    void init() override {
        ChatScreen::init();
        show_suggestions = true;
        if (lines_.size() != 0 || manager == nullptr || manager->font == nullptr ||
            manager->options == nullptr) {
            return;
        }
        Style blue;
        blue.colour = 0x55FFFFu;
        blue.underlined = true;
        blue.hover_action = "show_text";
        blue.hover_text = std::make_shared<const Component>(literal("Open the tier list"));
        blue.click_action = "open_url";
        blue.click_value = "https://bradgesmp.nl/tiers";
        Component linked = literal("See the tiers at ");
        linked.extra.push_back(literal("bradgesmp.nl/tiers", blue));
        for (const Component& line :
             {literal("<treur> ready for the duel?"), literal("<220t> one moment"),
              literal("treur fell out of the world"), linked}) {
            lines_.add(line, 0, 0, *manager->font, *manager->options, true);
        }
    }

private:
    hud::Chat lines_;
    proto::CommandTree tree_;
};

} // namespace

const CreativeTabs& creative_tabs() {
    static const CreativeTabs tabs = [] {
        ItemTable items;
        items.load(ItemTable::default_path(protocol::kTargetProtocol));
        CreativeTabs out;
        out.load(CreativeTabs::default_path(protocol::kTargetProtocol), items);
        return out;
    }();
    return tabs;
}

const std::vector<ScreenName>& screen_names() {
    static const std::vector<ScreenName> names = {
        {"inventory", true},
        {"creative", true},
        {"crafting", true},
        {"title", false},
        {"pause", true},
        // F: the multiplayer path, the options screens, chat and death.
        {"multiplayer", false},
        {"addserver", false},
        {"editserver", false},
        {"directjoin", false},
        {"connect", false},
        {"disconnected", false},
        {"confirm", false},
        {"options", false},
        {"video", false},
        {"controls", false},
        {"keybinds", false},
        {"mouse", false},
        {"chatoptions", false},
        {"sound", false},
        {"skin", false},
        {"accessibility", false},
        {"font", false},
        {"language", false},
        {"online", false},
        {"chat", true},
        {"death", true},
    };
    return names;
}

std::shared_ptr<Screen> make_screen(std::string_view name) {
    if (name == "inventory") return std::make_shared<InventoryScreen>();
    if (name == "creative") return std::make_shared<CreativeModeInventoryScreen>(&creative_tabs(), false);
    if (name == "crafting") return std::make_shared<CraftingScreen>(translatable("container.crafting"));
    if (name == "title") return std::make_shared<TitleScreen>();
    if (name == "pause") return std::make_shared<PauseScreen>(true);
    // F: the multiplayer path.
    if (name == "multiplayer") return std::make_shared<ShownMultiplayerScreen>();
    if (name == "addserver" || name == "editserver") {
        static ServerEntry entry = [] {
            ServerEntry made;
            made.name = "BradgeSMP";
            made.ip = "mc.bradgesmp.nl";
            return made;
        }();
        const bool adding = name == "addserver";
        return std::make_shared<ManageServerScreen>(
            nullptr, translatable(adding ? "manageServer.add.title" : "manageServer.edit.title"),
            &entry, [](bool) {});
    }
    if (name == "directjoin") {
        static ServerEntry entry;
        return std::make_shared<DirectJoinServerScreen>(nullptr, &entry, [](bool) {});
    }
    if (name == "connect") {
        return std::make_shared<ConnectScreen>(nullptr, translatable("connect.failed"));
    }
    if (name == "disconnected") {
        return std::make_shared<DisconnectedScreen>(
            nullptr, translatable("disconnect.lost"),
            translatable("disconnect.genericReason",
                         {literal("Internal Exception: timed out")}));
    }
    if (name == "confirm") {
        return std::make_shared<ConfirmScreen>(
            [](bool) {}, translatable("selectServer.deleteQuestion"),
            translatable("selectServer.deleteWarning", {literal("BradgeSMP")}),
            translatable("selectServer.deleteButton"), translatable("gui.cancel"));
    }
    // F: the options screens.
    if (name == "options") return std::make_shared<OptionsScreen>(nullptr, shown_options());
    if (name == "video") return std::make_shared<VideoSettingsScreen>(nullptr, shown_options());
    if (name == "controls") {
        return std::make_shared<ControlsScreen>(nullptr, shown_options(), shown_keybinds());
    }
    if (name == "keybinds") {
        return std::make_shared<KeyBindsScreen>(nullptr, shown_options(), shown_keybinds());
    }
    if (name == "mouse") return std::make_shared<MouseSettingsScreen>(nullptr, shown_options());
    if (name == "chatoptions") {
        return std::make_shared<ChatOptionsScreen>(nullptr, shown_options());
    }
    if (name == "sound") return std::make_shared<SoundOptionsScreen>(nullptr, shown_options());
    if (name == "skin") {
        return std::make_shared<SkinCustomizationScreen>(nullptr, shown_options());
    }
    if (name == "accessibility") {
        return std::make_shared<AccessibilityOptionsScreen>(nullptr, shown_options(),
                                                            shown_keybinds());
    }
    if (name == "font") return std::make_shared<FontOptionsScreen>(nullptr, shown_options());
    if (name == "language") {
        return std::make_shared<LanguageSelectScreen>(nullptr, shown_options());
    }
    if (name == "online") return std::make_shared<OnlineOptionsScreen>(nullptr, shown_options());
    // F: chat and death.
    if (name == "chat") return std::make_shared<ShownChatScreen>();
    if (name == "death") {
        return std::make_shared<DeathScreen>(
            translatable("death.attack.player", {literal("220t"), literal("PlasmaS")}), false, 42);
    }
    return nullptr;
}

} // namespace mcpp::ui
