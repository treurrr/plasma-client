#include "mcpp/ui/multiplayer.hpp"

#include "mcpp/image_png.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace mcpp::ui {
namespace {

constexpr int kTopRowButtonWidth = 100;
constexpr int kLowerRowButtonWidth = 74;
constexpr int kIconSize = 32;
constexpr int kSpacing = 5;
constexpr int kStatusIconWidth = 10;
constexpr int kStatusIconHeight = 8;
constexpr uint32_t kGrey = 0xFF808080u;
constexpr uint32_t kLabelGrey = 0xFFA0A0A0u;
constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kIconShade = 0xA0909090u;

Style red() {
    Style style;
    style.colour = 0xFF0000u;
    return style;
}

int text_width(const Screen& screen, const Component& component) {
    const auto* manager = screen.manager;
    return manager != nullptr && manager->font != nullptr ? manager->font->width(component) : 0;
}

// `SelectableEntry.mouseOverRightHalf`, `mouseOverTopLeftQuarter`, `mouseOverBottomLeftQuarter`.
bool over_right_half(int x, int y, int side) {
    return x >= side / 2 && x < side && y >= 0 && y < side;
}
bool over_top_left_quarter(int x, int y, int side) {
    return x >= 0 && x < side / 2 && y >= 0 && y < side / 2;
}
bool over_bottom_left_quarter(int x, int y, int side) {
    return x >= 0 && x < side / 2 && y >= side / 2 && y < side;
}

// The game's own ping bars, by how long the answer took.
const char* ping_sprite(int64_t ping) {
    if (ping < 150) return "server_list/ping_5";
    if (ping < 300) return "server_list/ping_4";
    if (ping < 600) return "server_list/ping_3";
    if (ping < 1000) return "server_list/ping_2";
    return "server_list/ping_1";
}

// The dynamic texture layer a row's icon lives in; sixty-four is the whole
// supply, which is more than a server list ever shows at once.
uint32_t icon_layer(size_t index) { return static_cast<uint32_t>(index % 60) + 4; }

} // namespace

// `ServerAddress.isValidAddress`: Guava's HostAndPort with a non-empty host.
bool is_valid_address(std::string_view address) {
    if (address.empty()) return false;
    std::string_view host = address;
    std::string_view port;
    if (address.front() == '[') {
        const size_t close = address.find(']');
        if (close == std::string_view::npos) return false;
        host = address.substr(1, close - 1);
        const std::string_view rest = address.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return false;
            port = rest.substr(1);
        }
    } else {
        const size_t colon = address.find(':');
        if (colon != std::string_view::npos) {
            if (address.find(':', colon + 1) != std::string_view::npos) return false;
            host = address.substr(0, colon);
            port = address.substr(colon + 1);
        }
    }
    if (host.empty()) return false;
    if (!port.empty()) {
        if (port.size() > 5) return false;
        for (const char c : port) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
        }
        if (std::strtol(std::string(port).c_str(), nullptr, 10) > 65535) return false;
    }
    return true;
}

// `ServerAddress.parseString`: the host and its port, 25565 when none is given.
std::pair<std::string, uint16_t> parse_address(std::string_view address) {
    if (!is_valid_address(address)) return {"server.invalid", 25565};
    std::string_view host = address;
    std::string_view port;
    if (address.front() == '[') {
        const size_t close = address.find(']');
        host = address.substr(1, close - 1);
        const std::string_view rest = address.substr(close + 1);
        if (!rest.empty()) port = rest.substr(1);
    } else {
        const size_t colon = address.find(':');
        if (colon != std::string_view::npos) {
            host = address.substr(0, colon);
            port = address.substr(colon + 1);
        }
    }
    uint16_t number = 25565;
    if (!port.empty()) {
        number = static_cast<uint16_t>(std::strtol(std::string(port).c_str(), nullptr, 10));
    }
    return {std::string(host), number};
}

ServerPinger::~ServerPinger() { wait_all(); }

void ServerPinger::wait_all() {
    for (auto& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
    threads_.clear();
}

// `ServerStatusPinger.pingServer`: the status and then the ping, on a thread
// of its own, with the result taken in on the next tick.
void ServerPinger::ping(size_t index, std::string ip, int32_t protocol) {
    ++running_;
    threads_.emplace_back([this, index, ip = std::move(ip), protocol] {
        Answer answer;
        answer.index = index;
        answer.ip = ip;
        const auto [host, port] = parse_address(ip);
        if (host == "server.invalid") {
            answer.resolved = false;
        } else {
            status::Request request;
            request.host = host;
            request.port = port;
            request.protocol = protocol;
            try {
                answer.reply = status::query(request);
                answer.reached = true;
            } catch (const std::exception&) {
                answer.reached = false;
            }
        }
        {
            std::lock_guard lock(guard_);
            answers_.push_back(std::move(answer));
        }
        --running_;
    });
}

// `ServerSelectionList.OnlineServerEntry.refreshStatus`: what the answer puts
// on the row.
void ServerPinger::take(ServerList& list) {
    std::vector<Answer> taken;
    {
        std::lock_guard lock(guard_);
        taken.swap(answers_);
    }
    for (auto& answer : taken) {
        if (answer.index >= list.entries.size()) continue;
        auto& entry = list.entries[answer.index];
        if (entry.ip != answer.ip) continue;
        if (!answer.resolved) {
            entry.state = ServerEntry::State::Unreachable;
            entry.motd = "Can't resolve hostname";
            continue;
        }
        if (!answer.reached) {
            entry.state = ServerEntry::State::Unreachable;
            entry.motd = "Can't connect to server";
            continue;
        }
        entry.ping = static_cast<int64_t>(answer.reply.latency_ms);
        entry.version = answer.reply.version;
        entry.protocol = answer.reply.protocol;
        entry.players_online = answer.reply.online;
        entry.players_max = answer.reply.max;
        entry.motd = answer.reply.motd;
        entry.player_list.clear();
        for (const auto& [player, uuid] : answer.reply.sample) entry.player_list.push_back(player);
        entry.icon_png = answer.reply.favicon_png;
        entry.state = answer.reply.protocol == protocol::kTargetProtocol
                          ? ServerEntry::State::Successful
                          : ServerEntry::State::Incompatible;
    }
}

namespace {

// `ServerSelectionList.OnlineServerEntry`: a server from the file, with the
// icon, the motd, the ping bars, the version and the hover controls.
class OnlineServerEntry : public EntryList::Entry {
public:
    OnlineServerEntry(JoinMultiplayerScreen& owner, size_t at) : screen(owner), index(at) {}

    void render_content(GuiGraphics& gfx, double mx, double my, bool hovered) override;
    bool mouse_clicked(double mx, double my, int button, bool double_click) override;
    bool key_pressed(const KeyEvent& event) override;
    Component narration() const override;

    ServerEntry& data() const { return screen.servers.entries[index]; }

    JoinMultiplayerScreen& screen;
    size_t index;

private:
    void upload_icon(GuiGraphics& gfx);

    std::vector<uint8_t> uploaded_;
    bool uploaded_any_ = false;
};

void OnlineServerEntry::upload_icon(GuiGraphics& gfx) {
    auto& entry = data();
    if (entry.icon_png == uploaded_ && uploaded_any_) return;
    uploaded_ = entry.icon_png;
    uploaded_any_ = true;
    if (entry.icon_png.empty() && !entry.icon.empty()) {
        const auto bytes = base64_decode(entry.icon);
        if (const auto image = read_png(bytes)) {
            gfx.quads.set_dynamic(icon_layer(index), image->rgba, image->width, image->height);
            return;
        }
    }
    if (!entry.icon_png.empty()) {
        if (const auto image = read_png(entry.icon_png)) {
            gfx.quads.set_dynamic(icon_layer(index), image->rgba, image->width, image->height);
            entry.icon = base64_encode(entry.icon_png);
        }
    }
}

// `ServerSelectionList.OnlineServerEntry.renderContent`: the ping is started
// the first time the row is drawn, then the name, two lines of motd, the icon,
// the status icon and the version, and the hover controls over the icon.
void OnlineServerEntry::render_content(GuiGraphics& gfx, double mx, double my, bool hovered) {
    auto& entry = data();
    if (entry.state == ServerEntry::State::Initial) {
        entry.state = ServerEntry::State::Pinging;
        entry.motd.clear();
        screen.pinger.ping(index, entry.ip, -1);
    }
    gfx.draw_string(entry.name, content_x() + kIconSize + 3, content_y() + 1, kWhite);
    const auto motd = gfx.wrap(literal(entry.motd), content_width() - kIconSize - 2);
    for (size_t line = 0; line < std::min<size_t>(motd.size(), 2); ++line) {
        gfx.draw_string(motd[line], content_x() + kIconSize + 3,
                        content_y() + 12 + 9 * static_cast<int>(line), kGrey);
    }
    upload_icon(gfx);
    gfx.quads.dynamic(icon_layer(index), static_cast<float>(content_x() * gfx.scale),
                      static_cast<float>(content_y() * gfx.scale),
                      static_cast<float>(kIconSize * gfx.scale),
                      static_cast<float>(kIconSize * gfx.scale));

    std::string icon_name;
    Component icon_tooltip;
    switch (entry.state) {
    case ServerEntry::State::Initial:
    case ServerEntry::State::Pinging: {
        // One of the five pinging frames, by the clock and the row's place.
        int frame = static_cast<int>((gfx.millis / 100LL + static_cast<int64_t>(index) * 2) & 7LL);
        if (frame > 4) frame = 8 - frame;
        icon_name = std::string("server_list/pinging_") +
                    std::to_string(frame >= 1 && frame <= 4 ? frame + 1 : 1);
        icon_tooltip = translatable("multiplayer.status.pinging");
        break;
    }
    case ServerEntry::State::Incompatible:
        icon_name = "server_list/incompatible";
        icon_tooltip = translatable("multiplayer.status.incompatible");
        break;
    case ServerEntry::State::Unreachable:
        icon_name = "server_list/unreachable";
        icon_tooltip = translatable("multiplayer.status.no_connection");
        break;
    case ServerEntry::State::Successful:
        icon_name = ping_sprite(entry.ping);
        icon_tooltip = translatable("multiplayer.status.ping",
                                   {literal(std::to_string(entry.ping))});
        break;
    }
    const int icon_x = content_right() - kStatusIconWidth - kSpacing;
    gfx.blit_sprite(icon_name, icon_x, content_y(), kStatusIconWidth, kStatusIconHeight);

    // `ServerStatusPinger.formatPlayerCount`: online over max, and the
    // version in red when the protocol does not match.
    Component right;
    if (entry.state == ServerEntry::State::Incompatible) {
        right = literal(entry.version, red());
    } else if (entry.state == ServerEntry::State::Successful) {
        right = entry.players_online >= 0
                    ? translatable("multiplayer.status.player_count",
                                   {literal(std::to_string(entry.players_online)),
                                    literal(std::to_string(entry.players_max))})
                    : translatable("multiplayer.status.unknown");
    }
    const int right_width = gfx.width_of(right);
    const int right_x = icon_x - right_width - kSpacing;
    gfx.draw_string(right, right_x, content_y() + 1, kGrey);
    if (mx >= icon_x && mx <= icon_x + kStatusIconWidth && my >= content_y() &&
        my <= content_y() + kStatusIconHeight) {
        gfx.set_tooltip_for_next_frame(icon_tooltip, static_cast<int>(mx), static_cast<int>(my));
    } else if (!entry.player_list.empty() && mx >= right_x && mx <= right_x + right_width &&
               my >= content_y() && my <= content_y() - 1 + 9) {
        std::vector<std::vector<Run>> lines;
        for (const auto& player : entry.player_list) {
            lines.push_back(gfx.runs(literal(player)));
        }
        gfx.set_tooltip_for_next_frame(lines, static_cast<int>(mx), static_cast<int>(my));
    }

    if (!hovered) return;
    gfx.fill(content_x(), content_y(), content_x() + kIconSize, content_y() + kIconSize,
             kIconShade);
    const int in_x = static_cast<int>(mx) - content_x();
    const int in_y = static_cast<int>(my) - content_y();
    const bool join = over_right_half(in_x, in_y, kIconSize);
    gfx.blit_sprite(join ? "server_list/join_highlighted" : "server_list/join", content_x(),
                    content_y(), kIconSize, kIconSize);
    const auto shown = screen.servers.visible();
    if (index > 0) {
        const bool up = over_top_left_quarter(in_x, in_y, kIconSize);
        gfx.blit_sprite(up ? "server_list/move_up_highlighted" : "server_list/move_up",
                        content_x(), content_y(), kIconSize, kIconSize);
    }
    if (index + 1 < shown.size()) {
        const bool down = over_bottom_left_quarter(in_x, in_y, kIconSize);
        gfx.blit_sprite(down ? "server_list/move_down_highlighted" : "server_list/move_down",
                        content_x(), content_y(), kIconSize, kIconSize);
    }
}

// `ServerSelectionList.OnlineServerEntry.mouseClicked`: the right half joins,
// the two left quarters move the row, and a double click joins as well.
bool OnlineServerEntry::mouse_clicked(double mx, double my, int, bool double_click) {
    const int in_x = static_cast<int>(mx) - content_x();
    const int in_y = static_cast<int>(my) - content_y();
    if (over_right_half(in_x, in_y, kIconSize)) {
        screen.join(data());
        return true;
    }
    const auto shown = screen.servers.visible();
    if (index > 0 && over_top_left_quarter(in_x, in_y, kIconSize)) {
        screen.swap_rows(index, index - 1);
        return true;
    }
    if (index + 1 < shown.size() && over_bottom_left_quarter(in_x, in_y, kIconSize)) {
        screen.swap_rows(index, index + 1);
        return true;
    }
    if (double_click) screen.join(data());
    return true;
}

// `ServerSelectionList.OnlineServerEntry.keyPressed`: Enter joins, shift with
// an arrow moves the row.
bool OnlineServerEntry::key_pressed(const KeyEvent& event) {
    if (event.key == key::Enter || event.key == key::Space || event.key == key::KeypadEnter) {
        screen.join(data());
        return true;
    }
    if (event.shift()) {
        const auto shown = screen.servers.visible();
        if (event.key == key::Down && index + 1 < shown.size()) {
            screen.swap_rows(index, index + 1);
            return true;
        }
        if (event.key == key::Up && index > 0) {
            screen.swap_rows(index, index - 1);
            return true;
        }
    }
    return false;
}

Component OnlineServerEntry::narration() const {
    return translatable("narrator.select", {literal(data().name)});
}

// `ServerSelectionList.LANHeader`: the line that says the network is being
// looked at. This client scans no network, so the row only says so.
class LanHeaderEntry : public EntryList::Entry {
public:
    explicit LanHeaderEntry(int label_width)
        : dots_(label_width, translatable("lanServer.scanning")) {}

    void render_content(GuiGraphics& gfx, double mx, double my, bool) override {
        dots_.set_position(content_x_middle() - dots_.w / 2, content_y());
        dots_.render(gfx, mx, my);
    }
    Component narration() const override { return translatable("lanServer.scanning"); }

private:
    LoadingDotsWidget dots_;
};

} // namespace

ServerSelectionList::ServerSelectionList(JoinMultiplayerScreen& owner, int list_w, int list_h,
                                         int top)
    : EntryList(0, top, list_w, list_h, 36), screen(owner) {}

// `ServerSelectionList.updateOnlineServers`, `refreshEntries`: a row per
// listed server and then the local network header.
void ServerSelectionList::update_online_servers(ServerList& list) {
    const auto previous = selected_index();
    clear_entries();
    const auto shown = list.visible();
    for (size_t i = 0; i < shown.size(); ++i) {
        add_entry(std::make_unique<OnlineServerEntry>(screen, shown[i]));
    }
    const int label = screen.manager != nullptr && screen.manager->font != nullptr
                          ? screen.manager->font->width(translatable("lanServer.scanning"))
                          : 0;
    add_entry(std::make_unique<LanHeaderEntry>(label));
    if (previous && *previous < size()) set_selected(previous);
}

JoinMultiplayerScreen::JoinMultiplayerScreen(std::shared_ptr<Screen> back)
    : Screen(translatable("multiplayer.title")), file(ServerList::default_path()) {
    parent = std::move(back);
}

// `JoinMultiplayerScreen.init`: the title in a 33 high header, the list between, and in a 60 high footer a row of three 100 wide buttons over a row of four 74 wide ones.
void JoinMultiplayerScreen::init() {
    auto& frame = layout<HeaderAndFooterLayout>(width, height, 33, 60);
    const int title_w = text_width(*this, title);
    frame.add_to_header(add<StringWidget>(0, 0, title_w, 9, title));
    if (servers.entries.empty()) {
        if (!servers.load(file)) {
            if (const auto vanilla = ServerList::vanilla_path()) servers.load(*vanilla);
        }
    }
    auto& selection = add<ServerSelectionList>(*this, width, frame.content_height(),
                                               frame.header_height());
    list = &selection;
    selection.in_world = manager != nullptr && manager->in_world;
    selection.on_select = [this](std::optional<size_t>) { on_selected_change(); };
    frame.add_to_contents(selection);
    selection.update_online_servers(servers);

    auto& column = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    column.spacing(4);
    column.default_cell_setting().align_horizontally(0.5f);
    auto& top = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    top.spacing(4);
    auto& bottom = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    bottom.spacing(4);
    column.add_child(top);
    column.add_child(bottom);
    frame.add_to_footer(column);

    select_button_ = &top.add_child(
        add<Button>(0, 0, kTopRowButtonWidth, 20, translatable("selectServer.select"),
                    [this](Button&) {
                        if (list == nullptr) return;
                        if (auto* row = dynamic_cast<OnlineServerEntry*>(list->selected())) {
                            join(row->data());
                        }
                    }));
    top.add_child(add<Button>(0, 0, kTopRowButtonWidth, 20, translatable("selectServer.direct"),
                              [this](Button&) {
                                  editing = ServerEntry{};
                                  editing.name = manager != nullptr && manager->font != nullptr
                                                     ? manager->font->tr("selectServer.defaultName")
                                                     : "Minecraft Server";
                                  if (manager == nullptr) return;
                                  self_ = manager->current_shared();
                                  manager->set_screen(std::make_shared<DirectJoinServerScreen>(
                                      manager->current_shared(), &editing,
                                      [this](bool ok) { direct_join_callback(ok); }));
                              }));
    top.add_child(add<Button>(
        0, 0, kTopRowButtonWidth, 20, translatable("selectServer.add"), [this](Button&) {
            editing = ServerEntry{};
            if (manager == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<ManageServerScreen>(
                manager->current_shared(), translatable("manageServer.add.title"), &editing,
                [this](bool ok) { add_server_callback(ok); }));
        }));
    edit_button_ = &bottom.add_child(add<Button>(
        0, 0, kLowerRowButtonWidth, 20, translatable("selectServer.edit"), [this](Button&) {
            if (list == nullptr || manager == nullptr) return;
            auto* row = dynamic_cast<OnlineServerEntry*>(list->selected());
            if (row == nullptr) return;
            editing = row->data();
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<ManageServerScreen>(
                manager->current_shared(), translatable("manageServer.edit.title"), &editing,
                [this](bool ok) { edit_server_callback(ok); }));
        }));
    delete_button_ = &bottom.add_child(add<Button>(
        0, 0, kLowerRowButtonWidth, 20, translatable("selectServer.delete"), [this](Button&) {
            if (list == nullptr || manager == nullptr) return;
            auto* row = dynamic_cast<OnlineServerEntry*>(list->selected());
            if (row == nullptr) return;
            self_ = manager->current_shared();
            manager->set_screen(std::make_shared<ConfirmScreen>(
                [this](bool ok) { delete_callback(ok); },
                translatable("selectServer.deleteQuestion"),
                translatable("selectServer.deleteWarning", {literal(row->data().name)}),
                translatable("selectServer.deleteButton"), translatable("gui.cancel")));
        }));
    bottom.add_child(add<Button>(0, 0, kLowerRowButtonWidth, 20,
                                 translatable("selectServer.refresh"),
                                 [this](Button&) { refresh_list(); }));
    bottom.add_child(add<Button>(0, 0, kLowerRowButtonWidth, 20, translatable("gui.back"),
                                 [this](Button&) { on_close(); }));

    frame.arrange_elements();
    selection.update_size(width, frame.content_height(), frame.header_height());
    on_selected_change();
}

void JoinMultiplayerScreen::tick() {
    Screen::tick();
    pinger.take(servers);
}

void JoinMultiplayerScreen::removed() { pinger.wait_all(); }

// `JoinMultiplayerScreen.keyPressed`: F5 builds the list again.
bool JoinMultiplayerScreen::key_pressed(const KeyEvent& event) {
    if (Screen::key_pressed(event)) return true;
    if (event.key == 294) {
        refresh_list();
        return true;
    }
    return false;
}

bool JoinMultiplayerScreen::mouse_clicked(double mx, double my, int button) {
    if (list != nullptr) list->double_click = double_click;
    return Screen::mouse_clicked(mx, my, button);
}

// `JoinMultiplayerScreen.onSelectedChange`: Join on any row, Edit and Delete
// only on a server from the file.
void JoinMultiplayerScreen::on_selected_change() {
    if (select_button_ != nullptr) select_button_->active = false;
    if (edit_button_ != nullptr) edit_button_->active = false;
    if (delete_button_ != nullptr) delete_button_->active = false;
    if (list == nullptr) return;
    auto* row = list->selected();
    if (row == nullptr || dynamic_cast<LanHeaderEntry*>(row) != nullptr) return;
    if (select_button_ != nullptr) select_button_->active = true;
    if (dynamic_cast<OnlineServerEntry*>(row) != nullptr) {
        if (edit_button_ != nullptr) edit_button_->active = true;
        if (delete_button_ != nullptr) delete_button_->active = true;
    }
}

void JoinMultiplayerScreen::save_servers() { servers.save(file); }

void JoinMultiplayerScreen::swap_rows(size_t a, size_t b) {
    servers.swap(a, b);
    save_servers();
    if (list != nullptr) list->swap_entries(a, b);
    if (list != nullptr) list->update_online_servers(servers);
}

// `JoinMultiplayerScreen.join`: the connect screen takes over.
void JoinMultiplayerScreen::join(const ServerEntry& entry) {
    if (manager == nullptr) return;
    auto connect = std::make_shared<ConnectScreen>(manager->current_shared(),
                                                   translatable("connect.failed"));
    const std::string address = entry.ip;
    manager->set_screen(connect);
    if (manager->host.connect) manager->host.connect(address);
}

// `JoinMultiplayerScreen.refreshServerList`: a new screen of the same kind.
void JoinMultiplayerScreen::refresh_list() {
    if (manager == nullptr) return;
    manager->set_screen(std::make_shared<JoinMultiplayerScreen>(parent));
}

// `JoinMultiplayerScreen.addServerCallback`: a hidden entry with that address
// comes back, else the entry is added, and the file is written either way.
void JoinMultiplayerScreen::add_server_callback(bool ok) {
    if (ok) {
        if (ServerEntry* hidden = servers.unhide(editing.ip)) {
            hidden->name = editing.name;
            hidden->icon = editing.icon;
        } else {
            servers.add(editing, false);
        }
        save_servers();
        if (list != nullptr) {
            list->set_selected(std::nullopt);
            list->update_online_servers(servers);
        }
    }
    if (manager != nullptr) manager->set_screen(self_);
}

// `JoinMultiplayerScreen.editServerCallback`: the row takes the new name,
// address and pack setting.
void JoinMultiplayerScreen::edit_server_callback(bool ok) {
    if (ok && list != nullptr) {
        if (auto* row = dynamic_cast<OnlineServerEntry*>(list->selected())) {
            auto& entry = row->data();
            entry.name = editing.name;
            entry.ip = editing.ip;
            entry.packs = editing.packs;
            entry.state = ServerEntry::State::Initial;
            save_servers();
            list->update_online_servers(servers);
        }
    }
    if (manager != nullptr) manager->set_screen(self_);
}

// `JoinMultiplayerScreen.directJoinCallback`: an address the file already has
// is joined as it stands, a new one is kept hidden first.
void JoinMultiplayerScreen::direct_join_callback(bool ok) {
    if (!ok) {
        if (manager != nullptr) manager->set_screen(self_);
        return;
    }
    if (ServerEntry* known = servers.find(editing.ip)) {
        join(*known);
        return;
    }
    servers.add(editing, true);
    save_servers();
    join(editing);
}

// `JoinMultiplayerScreen.deleteCallback`: the entry goes and the file is written.
void JoinMultiplayerScreen::delete_callback(bool ok) {
    if (ok && list != nullptr) {
        if (auto* row = dynamic_cast<OnlineServerEntry*>(list->selected())) {
            servers.remove(row->index);
            save_servers();
            list->set_selected(std::nullopt);
            list->update_online_servers(servers);
        }
    }
    if (manager != nullptr) manager->set_screen(self_);
}

ManageServerScreen::ManageServerScreen(std::shared_ptr<Screen> back, Component heading,
                                       ServerEntry* entry, std::function<void(bool)> done)
    : Screen(std::move(heading)), entry_(entry), done_(std::move(done)) {
    parent = std::move(back);
}

// `ManageServerScreen.init`: the name box at 66, the address box at 106, the pack cycle a quarter down plus 72, Done plus 114 and Cancel plus 138, all 200 wide.
void ManageServerScreen::init() {
    auto& name = add<EditBox>(width / 2 - 100, 66, 200, 20, translatable("manageServer.enterName"));
    name_edit = &name;
    name.set_value(entry_->name);
    name.hint = translatable("selectServer.defaultName");
    name.on_change = [this](const std::string&) { update_add_button(); };
    if (manager != nullptr) name.platform = &manager->platform;
    auto& ip = add<EditBox>(width / 2 - 100, 106, 200, 20, translatable("manageServer.enterIp"));
    ip_edit = &ip;
    ip.max_length = 128;
    ip.set_value(entry_->ip);
    ip.on_change = [this](const std::string&) { update_add_button(); };
    if (manager != nullptr) ip.platform = &manager->platform;

    const size_t packs = static_cast<size_t>(entry_->packs);
    auto& cycle = add<CycleButton>(
        width / 2 - 100, height / 4 + 72, 200, 20, translatable("manageServer.resourcePack"),
        std::vector<Component>{translatable("manageServer.resourcePack.enabled"),
                               translatable("manageServer.resourcePack.disabled"),
                               translatable("manageServer.resourcePack.prompt")},
        packs, [this](size_t index) {
            entry_->packs = static_cast<ServerEntry::Packs>(index);
        });
    (void)cycle;
    add_button_ = &add<Button>(width / 2 - 100, height / 4 + 96 + 18, 200, 20,
                               translatable("gui.done"), [this](Button&) { on_add(); });
    add<Button>(width / 2 - 100, height / 4 + 120 + 18, 200, 20, translatable("gui.cancel"),
                [this](Button&) {
                    if (done_) done_(false);
                });
    update_add_button();
    set_focus(name_edit, true);
}

// `ManageServerScreen.onAdd`: an empty name becomes the default one.
void ManageServerScreen::on_add() {
    const std::string typed = name_edit->value();
    entry_->name = typed.empty() ? (manager != nullptr && manager->font != nullptr
                                        ? manager->font->tr("selectServer.defaultName")
                                        : "Minecraft Server")
                                 : typed;
    entry_->ip = ip_edit->value();
    if (done_) done_(true);
}

void ManageServerScreen::update_add_button() {
    if (add_button_ != nullptr && ip_edit != nullptr) {
        add_button_->active = is_valid_address(ip_edit->value());
    }
}

// `ManageServerScreen.render`: the title at 17 and the two grey labels at 53 and 94.
void ManageServerScreen::render(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    gfx.draw_centered_string(title, width / 2, 17, kWhite);
    gfx.draw_string(translatable("manageServer.enterName"), width / 2 - 100 + 1, 53, kLabelGrey);
    gfx.draw_string(translatable("manageServer.enterIp"), width / 2 - 100 + 1, 94, kLabelGrey);
}

DirectJoinServerScreen::DirectJoinServerScreen(std::shared_ptr<Screen> back, ServerEntry* entry,
                                               std::function<void(bool)> done)
    : Screen(translatable("selectServer.direct")), entry_(entry), done_(std::move(done)) {
    parent = std::move(back);
}

// `DirectJoinServerScreen.init`: the address box at 116 holding lastServer, Join a quarter down plus 108 and Cancel plus 132.
void DirectJoinServerScreen::init() {
    auto& ip = add<EditBox>(width / 2 - 100, 116, 200, 20, translatable("manageServer.enterIp"));
    ip_edit = &ip;
    ip.max_length = 128;
    if (manager != nullptr && manager->options != nullptr) {
        if (const auto* last = manager->options->raw("lastServer")) ip.set_value(*last);
    }
    ip.on_change = [this](const std::string&) { update_select_button(); };
    if (manager != nullptr) ip.platform = &manager->platform;
    select_button_ = &add<Button>(width / 2 - 100, height / 4 + 96 + 12, 200, 20,
                                  translatable("selectServer.select"),
                                  [this](Button&) { on_select(); });
    add<Button>(width / 2 - 100, height / 4 + 120 + 12, 200, 20, translatable("gui.cancel"),
                [this](Button&) {
                    if (done_) done_(false);
                });
    update_select_button();
    set_focus(ip_edit, true);
}

// `DirectJoinServerScreen.removed`: the address typed becomes lastServer.
void DirectJoinServerScreen::removed() {
    if (manager == nullptr || manager->options == nullptr || ip_edit == nullptr) return;
    manager->options->set_raw("lastServer", ip_edit->value());
    manager->options->save(Options::default_path());
}

void DirectJoinServerScreen::on_select() {
    entry_->ip = ip_edit->value();
    if (done_) done_(true);
}

void DirectJoinServerScreen::update_select_button() {
    if (select_button_ != nullptr && ip_edit != nullptr) {
        select_button_->active = is_valid_address(ip_edit->value());
    }
}

// `DirectJoinServerScreen.keyPressed`: Enter in the box joins.
bool DirectJoinServerScreen::key_pressed(const KeyEvent& event) {
    const bool enter = event.key == key::Enter || event.key == key::KeypadEnter;
    if (select_button_ != nullptr && select_button_->active && focus() == ip_edit && enter) {
        on_select();
        return true;
    }
    return Screen::key_pressed(event);
}

// `DirectJoinServerScreen.render`: the title at 20 and the grey label at 100.
void DirectJoinServerScreen::render(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    gfx.draw_centered_string(title, width / 2, 20, kWhite);
    gfx.draw_string(translatable("manageServer.enterIp"), width / 2 - 100 + 1, 100, kLabelGrey);
}

ConnectScreen::ConnectScreen(std::shared_ptr<Screen> back, Component failed_title)
    : Screen(literal("")), connect_failed_title(std::move(failed_title)) {
    parent = std::move(back);
}

// `ConnectScreen.init`: Cancel a quarter down plus 132, 200 wide.
void ConnectScreen::init() {
    add<Button>(width / 2 - 100, height / 4 + 120 + 12, 200, 20, translatable("gui.cancel"),
                [this](Button&) {
                    aborted = true;
                    if (manager != nullptr && manager->host.cancel_connect) {
                        manager->host.cancel_connect();
                    }
                    if (manager != nullptr) manager->set_screen(parent);
                });
}

// `ConnectScreen.render`: the stage centred fifty above the middle.
void ConnectScreen::render(GuiGraphics& gfx, double mx, double my) {
    Screen::render(gfx, mx, my);
    gfx.draw_centered_string(status, width / 2, height / 2 - 50, kWhite);
}

DisconnectedScreen::DisconnectedScreen(std::shared_ptr<Screen> back, Component heading,
                                       Component why, Component button)
    : Screen(std::move(heading)), reason(std::move(why)), button_text(std::move(button)) {
    parent = std::move(back);
}

// `DisconnectedScreen.init`: the title, the reason wrapped to fifty short of the screen and centred, then the way back, 200 wide, the column centred on the screen with ten of padding and two around the button.
void DisconnectedScreen::init() {
    auto& column = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    column.default_cell_setting().align_horizontally(0.5f).padding(10);
    const int title_w = text_width(*this, title);
    column.add_child(add<StringWidget>(0, 0, title_w, 9, title));
    auto& text = add<MultiLineTextWidget>(reason, width - 50, true);
    if (manager != nullptr && manager->font != nullptr) text.measure(*manager->font);
    column.add_child(text);
    column.default_cell_setting().padding(2);
    column.add_child(add<Button>(0, 0, 200, 20, button_text, [this](Button&) {
        if (manager != nullptr) manager->set_screen(parent);
    }));
    column.arrange_elements();
    FrameLayout::center_in_rectangle(column, 0, 0, width, height);
}

ConfirmScreen::ConfirmScreen(std::function<void(bool)> callback, Component heading, Component body,
                            Component yes, Component no)
    : Screen(std::move(heading)), message(std::move(body)), yes_text(std::move(yes)),
      no_text(std::move(no)), callback_(std::move(callback)) {}

// `ConfirmScreen.init`: the title, the message wrapped to fifty short of the screen and centred, then the two buttons side by side sixteen under it, the column centred.
void ConfirmScreen::init() {
    auto& column = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Vertical);
    column.spacing(8);
    column.default_cell_setting().align_horizontally(0.5f);
    const int title_w = text_width(*this, title);
    column.add_child(add<StringWidget>(0, 0, title_w, 9, title));
    auto& text = add<MultiLineTextWidget>(message, width - 50, true);
    if (manager != nullptr && manager->font != nullptr) text.measure(*manager->font);
    column.add_child(text);
    auto& row = layout<LinearLayout>(0, 0, LinearLayout::Orientation::Horizontal);
    row.spacing(4);
    row.default_cell_setting().padding(0, 16, 0, 0);
    auto& cell = column.add_child(row);
    (void)cell;
    yes_button_ = &row.add_child(add<Button>(0, 0, 150, 20, yes_text, [this](Button&) {
        if (callback_) callback_(true);
    }));
    no_button_ = &row.add_child(add<Button>(0, 0, 150, 20, no_text, [this](Button&) {
        if (callback_) callback_(false);
    }));
    column.arrange_elements();
    FrameLayout::center_in_rectangle(column, 0, 0, width, height);
}

void ConfirmScreen::set_delay(int ticks) {
    delay_ = ticks;
    if (yes_button_ != nullptr) yes_button_->active = false;
    if (no_button_ != nullptr) no_button_->active = false;
}

void ConfirmScreen::tick() {
    Screen::tick();
    if (--delay_ == 0) {
        if (yes_button_ != nullptr) yes_button_->active = true;
        if (no_button_ != nullptr) no_button_->active = true;
    }
}

// `ConfirmScreen.keyPressed`: Escape is no once the delay has run out.
bool ConfirmScreen::key_pressed(const KeyEvent& event) {
    if (delay_ <= 0 && event.key == key::Escape) {
        if (callback_) callback_(false);
        return true;
    }
    return Screen::key_pressed(event);
}

} // namespace mcpp::ui
