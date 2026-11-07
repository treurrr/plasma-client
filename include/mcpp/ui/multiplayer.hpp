#pragma once
#include "mcpp/status.hpp"
#include "mcpp/ui/entry_list.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/server_list.hpp"
#include "mcpp/ui/widgets.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The multiplayer path: the server list, the screens that add and edit an
// entry, the direct connection, the wait while connecting and what is shown
// when the server lets go.
namespace mcpp::ui {

// `ServerAddress.isValidAddress`, `ServerAddress.parseString`: Guava's
// host:port, the port 25565 when none is written.
bool is_valid_address(std::string_view address);
std::pair<std::string, uint16_t> parse_address(std::string_view address);

// `ServerStatusPinger`: the status of every entry, asked for off the main
// thread and taken in on a tick.
class ServerPinger {
public:
    ~ServerPinger();

    // The entry is pinged at most once; it is the screen that decides when,
    // the way vanilla starts the ping the first time a row is drawn.
    void ping(size_t index, std::string ip, int32_t protocol);
    // Every answer that arrived, applied to the list it came from.
    void take(ServerList& list);
    void wait_all();
    size_t in_flight() const { return running_; }

private:
    struct Answer {
        size_t index = 0;
        std::string ip;
        bool reached = false;
        bool resolved = true;
        status::Reply reply;
    };

    std::vector<std::thread> threads_;
    std::mutex guard_;
    std::vector<Answer> answers_;
    std::atomic<size_t> running_{0};
};

class JoinMultiplayerScreen;

// `ServerSelectionList`: the rows of the server list, 36 high and 305 wide.
class ServerSelectionList : public EntryList {
public:
    ServerSelectionList(JoinMultiplayerScreen& screen, int list_w, int list_h, int top);
    int row_width() const override { return 305; }

    void update_online_servers(ServerList& servers);

    JoinMultiplayerScreen& screen;
};

class JoinMultiplayerScreen : public Screen {
public:
    explicit JoinMultiplayerScreen(std::shared_ptr<Screen> back);

    void init() override;
    void tick() override;
    void removed() override;
    bool key_pressed(const KeyEvent& event) override;
    bool mouse_clicked(double mx, double my, int button) override;

    void on_selected_change();
    void join(const ServerEntry& entry);
    void refresh_list();
    void save_servers();
    void swap_rows(size_t a, size_t b);

    ServerList servers;
    ServerPinger pinger;
    ServerSelectionList* list = nullptr;
    // The entry being added or edited, which the sub screen writes into.
    ServerEntry editing;
    // Where servers.dat is read from and written to; a test points it away
    // from the player's own file.
    std::filesystem::path file;

private:
    void add_server_callback(bool ok);
    void edit_server_callback(bool ok);
    void direct_join_callback(bool ok);
    void delete_callback(bool ok);

    // Taken while this screen is still the one on screen, so a sub screen's
    // answer can put it back.
    std::shared_ptr<Screen> self_;
    Button* select_button_ = nullptr;
    Button* edit_button_ = nullptr;
    Button* delete_button_ = nullptr;
};

// `ManageServerScreen`: the name, the address and the resource pack setting of
// one entry, used both to add and to edit.
class ManageServerScreen : public Screen {
public:
    ManageServerScreen(std::shared_ptr<Screen> back, Component heading, ServerEntry* entry,
                       std::function<void(bool)> done);

    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;

    EditBox* name_edit = nullptr;
    EditBox* ip_edit = nullptr;

private:
    void on_add();
    void update_add_button();

    ServerEntry* entry_;
    std::function<void(bool)> done_;
    Button* add_button_ = nullptr;
};

// `DirectJoinServerScreen`: one address box, filled with the last one used.
class DirectJoinServerScreen : public Screen {
public:
    DirectJoinServerScreen(std::shared_ptr<Screen> back, ServerEntry* entry,
                           std::function<void(bool)> done);

    void init() override;
    void removed() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool key_pressed(const KeyEvent& event) override;

    EditBox* ip_edit = nullptr;

private:
    void on_select();
    void update_select_button();

    ServerEntry* entry_;
    std::function<void(bool)> done_;
    Button* select_button_ = nullptr;
};

// `ConnectScreen`: the stage of the handshake and nothing but Cancel.
class ConnectScreen : public Screen {
public:
    ConnectScreen(std::shared_ptr<Screen> back, Component failed_title);

    void init() override;
    void render(GuiGraphics& gfx, double mx, double my) override;
    bool should_close_on_esc() const override { return false; }

    // What the client puts here as it goes: "connect.connecting",
    // "connect.authorizing", "connect.encrypting", "connect.joining".
    Component status = translatable("connect.connecting");
    Component connect_failed_title;
    bool aborted = false;
};

// `DisconnectedScreen`: the title, the reason wrapped, and the way back.
class DisconnectedScreen : public Screen {
public:
    DisconnectedScreen(std::shared_ptr<Screen> back, Component heading, Component why,
                       Component button = translatable("gui.toMenu"));

    void init() override;
    bool should_close_on_esc() const override { return false; }

    Component reason;
    Component button_text;
};

// `ConfirmScreen`: a question, two buttons, Escape counted as no.
class ConfirmScreen : public Screen {
public:
    ConfirmScreen(std::function<void(bool)> callback, Component heading, Component body,
                  Component yes = translatable("gui.yes"),
                  Component no = translatable("gui.no"));

    void init() override;
    void tick() override;
    bool should_close_on_esc() const override { return false; }
    bool key_pressed(const KeyEvent& event) override;
    void set_delay(int ticks);

    Component message;
    Component yes_text;
    Component no_text;

protected:
    std::function<void(bool)> callback_;
    Button* yes_button_ = nullptr;
    Button* no_button_ = nullptr;
    int delay_ = 0;
};

} // namespace mcpp::ui
