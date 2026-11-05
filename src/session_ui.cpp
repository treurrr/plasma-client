#include "mcpp/session.hpp"
#include "session_detail.hpp"

#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"

#include <algorithm>
#include <chrono>
#include <format>

namespace mcpp {
namespace {

constexpr size_t kSignatureBytes = 256;
constexpr size_t kAcknowledgedBytes = 3;   // Fixed BitSet(20)

// Layout read off the wire for 774: a signed message carries the plain text and,
// when the server decorated it, an NBT component that supersedes it.
// What player chat and disguised chat share after the message itself: a chat
// type, the sender, and maybe a target, written out the way the type says.
std::string format_chat(ByteBuffer& in, const std::string& body,
                        const nbt::TranslateLookup& lookup, const Registries& registries) {
    // The chat type says how the line is written out: a translation key and
    // which of sender, target and content fill it, in what order. Zero means
    // the type follows inline; anything else is one more than its index in
    // the chat_type registry sent in configuration.
    std::string key = "chat.type.text";
    std::vector<int32_t> order = {0, 2};
    const int32_t holder = in.read_varint();
    if (holder == 0) {
        key = in.read_string();
        order.clear();
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) order.push_back(in.read_varint());
        nbt::read_network_tag(in);   // the chat decoration's style
        in.read_string();            // and the narration, which this client does not speak
        const int32_t narrated = in.read_varint();
        for (int32_t i = 0; i < narrated; ++i) in.read_varint();
        nbt::read_network_tag(in);
    } else if (const auto* entry = registries.at("minecraft:chat_type",
                                                 static_cast<size_t>(holder - 1));
               entry != nullptr && entry->data.has_value()) {
        if (const auto* chat = entry->data->find("chat")) {
            if (const auto* text = chat->find("translation_key"); text && text->as_string()) {
                key = *text->as_string();
            }
            if (const auto* list = chat->find("parameters"); list && list->as_list()) {
                order.clear();
                for (const auto& item : *list->as_list()) {
                    const std::string* name = item.as_string();
                    if (name == nullptr) continue;
                    order.push_back(*name == "sender" ? 0 : *name == "target" ? 1 : 2);
                }
            }
        }
    }

    const std::string sender = nbt::component_to_text(nbt::read_network_tag(in), lookup);
    std::string target;
    if (in.read_bool()) target = nbt::component_to_text(nbt::read_network_tag(in), lookup);

    const std::string* pattern = lookup ? lookup(key) : nullptr;
    if (pattern == nullptr) return sender.empty() ? body : std::format("<{}> {}", sender, body);
    std::vector<std::string> args;
    for (const int32_t part : order) args.push_back(part == 0 ? sender : part == 1 ? target : body);
    return nbt::apply_pattern(*pattern, args);
}

std::string read_player_chat(ByteBuffer& in, const nbt::TranslateLookup& lookup,
                             const Registries& registries) {
    in.read_varint();
    in.read_bytes(detail::kUuidBytes);
    in.read_varint();
    if (in.read_bool()) in.read_bytes(kSignatureBytes);

    std::string body = in.read_string(256);
    in.read_i64();
    in.read_i64();

    const int32_t previous = in.read_varint();
    for (int32_t i = 0; i < previous; ++i) {
        if (in.read_varint() == 0) in.read_bytes(kSignatureBytes);
    }

    if (in.read_bool()) body = nbt::component_to_text(nbt::read_network_tag(in), lookup);

    if (in.read_varint() == 2) {
        const int32_t longs = in.read_varint();
        for (int32_t i = 0; i < longs; ++i) in.read_i64();
    }

    return format_chat(in, body, lookup, registries);
}

int64_t now_millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

namespace {

Scoreboard::NumberFormat read_number_format(ByteBuffer& in) {
    Scoreboard::NumberFormat format;
    if (!in.read_bool()) return format;
    switch (in.read_varint()) {
    case 0:
        format.kind = Scoreboard::NumberFormat::Kind::Blank;
        break;
    case 1:
        format.kind = Scoreboard::NumberFormat::Kind::Styled;
        format.value = nbt::read_network_tag(in);
        break;
    case 2:
        format.kind = Scoreboard::NumberFormat::Kind::Fixed;
        format.value = nbt::read_network_tag(in);
        break;
    default:
        break;
    }
    return format;
}

} // namespace

bool Session::handle_ui(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;
    peek_chat_components(packet);

    if (packet.id == cb.player_chat) {
        const auto text = json::strip_formatting(read_player_chat(in, lookup_, data_.registries));
        if (events_.ui.on_chat && !text.empty()) events_.ui.on_chat(text);
        return true;
    }
    if (packet.id == cb.disguised_chat) {
        // Chat with nobody signed behind it - the console, a command block, a
        // /say - so the message is a component rather than a signed string.
        const std::string body = nbt::component_to_text(nbt::read_network_tag(in), lookup_);
        const auto text = json::strip_formatting(format_chat(in, body, lookup_, data_.registries));
        if (events_.ui.on_chat && !text.empty()) events_.ui.on_chat(text);
        return true;
    }
    if (packet.id == cb.system_chat) {
        const auto text = detail::read_component(in, lookup_);
        // The last byte says it belongs over the hotbar rather than in the
        // chat: a minigame's countdown, a sleeping message, a server's hint.
        const bool action_bar = in.read_bool();
        if (text.empty()) return true;
        if (action_bar) {
            if (events_.ui.on_action_bar) events_.ui.on_action_bar(text);
        } else if (events_.ui.on_chat) {
            events_.ui.on_chat(text);
        }
        return true;
    }
    if (packet.id == cb.player_combat_kill) {
        const int32_t player = in.read_varint();
        const nbt::Value message = nbt::read_network_tag(in);
        if (player == entity_id_ && events_.ui.on_combat_kill) events_.ui.on_combat_kill(message);
        return true;
    }
    if (packet.id == cb.container_close) {
        const int32_t container = in.read_varint();
        if (container == player_.open_container) {
            player_.open_container = 0;
            player_.open_slots.clear();
        }
        if (events_.ui.on_container_close) events_.ui.on_container_close(container);
        return true;
    }
    if (packet.id == cb.open_screen) {
        const int32_t container = in.read_varint();
        const int32_t type = in.read_varint();
        const nbt::Value title = nbt::read_network_tag(in);
        player_.open_container = container;
        player_.open_state = 0;
        player_.open_slots.clear();
        if (events_.ui.on_open_screen) events_.ui.on_open_screen(container, type, title);
        return true;
    }
    if (packet.id == cb.place_ghost_recipe) {
        const int32_t container = in.read_varint();
        const proto::Value display = proto::read_named(in, "recipe_display", options_.protocol);
        if (events_.ui.on_place_ghost_recipe) events_.ui.on_place_ghost_recipe(container, display);
        return true;
    }
    if (packet.id == cb.server_links) {
        // The data area keeps the links too, so it reads them again.
        const size_t start = in.position();
        std::vector<std::pair<nbt::Value, std::string>> links;
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) {
            nbt::Value label;
            if (in.read_bool()) {
                label = nbt::Value(nbt::Value::Storage(static_cast<int32_t>(in.read_varint())));
            } else {
                label = nbt::read_network_tag(in);
            }
            links.emplace_back(std::move(label), in.read_string());
        }
        if (events_.ui.on_server_links) events_.ui.on_server_links(links);
        in.set_position(start);
        return false;
    }
    if (packet.id == cb.set_action_bar_text) {
        const nbt::Value text = nbt::read_network_tag(in);
        if (events_.ui.on_overlay_message) events_.ui.on_overlay_message(text);
        return true;
    }
    if (packet.id == cb.set_title_text) {
        const nbt::Value text = nbt::read_network_tag(in);
        if (events_.ui.on_title) events_.ui.on_title(text);
        return true;
    }
    if (packet.id == cb.set_subtitle_text) {
        const nbt::Value text = nbt::read_network_tag(in);
        if (events_.ui.on_subtitle) events_.ui.on_subtitle(text);
        return true;
    }
    if (packet.id == cb.set_titles_animation) {
        const int32_t fade_in = in.read_i32();
        const int32_t stay = in.read_i32();
        const int32_t fade_out = in.read_i32();
        if (events_.ui.on_title_times) events_.ui.on_title_times(fade_in, stay, fade_out);
        return true;
    }
    if (packet.id == cb.clear_titles) {
        const bool reset = in.read_bool();
        if (events_.ui.on_clear_titles) events_.ui.on_clear_titles(reset);
        return true;
    }
    if (packet.id == cb.tab_list) {
        const nbt::Value header = nbt::read_network_tag(in);
        const nbt::Value footer = nbt::read_network_tag(in);
        if (events_.ui.on_tab_list) events_.ui.on_tab_list(header, footer);
        return true;
    }
    if (packet.id == cb.boss_event) {
        BossEvent event;
        event.uuid = detail::read_uuid(in);
        event.op = static_cast<BossEvent::Op>(in.read_varint());
        switch (event.op) {
        case BossEvent::Op::Add:
            event.name = nbt::read_network_tag(in);
            event.progress = in.read_f32();
            event.colour = in.read_varint();
            event.overlay = in.read_varint();
            event.flags = in.read_u8();
            break;
        case BossEvent::Op::Remove:
            break;
        case BossEvent::Op::Progress:
            event.progress = in.read_f32();
            break;
        case BossEvent::Op::Name:
            event.name = nbt::read_network_tag(in);
            break;
        case BossEvent::Op::Style:
            event.colour = in.read_varint();
            event.overlay = in.read_varint();
            break;
        case BossEvent::Op::Flags:
            event.flags = in.read_u8();
            break;
        default:
            return true;
        }
        if (events_.ui.on_boss_event) events_.ui.on_boss_event(event);
        return true;
    }
    if (packet.id == cb.set_objective) {
        auto& board = ui_.scoreboard;
        std::string name = in.read_string();
        const uint8_t method = in.read_u8();
        if (method == 1) {
            board.objectives.erase(name);
            for (auto& slot : board.display) {
                if (slot == name) slot.clear();
            }
            for (auto& [owner, scores] : board.scores) scores.erase(name);
            return true;
        }
        auto& objective = board.objectives[name];
        objective.display_name = nbt::read_network_tag(in);
        objective.hearts = in.read_varint() == 1;
        objective.format = read_number_format(in);
        return true;
    }
    if (packet.id == cb.set_display_objective) {
        const int32_t slot = in.read_varint();
        std::string name = in.read_string();
        if (slot >= 0 && static_cast<size_t>(slot) < Scoreboard::kSlots) {
            ui_.scoreboard.display[static_cast<size_t>(slot)] = std::move(name);
        }
        return true;
    }
    if (packet.id == cb.set_score) {
        std::string owner = in.read_string();
        std::string objective = in.read_string();
        Scoreboard::Score score;
        score.value = in.read_varint();
        if (in.read_bool()) score.display_name = nbt::read_network_tag(in);
        score.format = read_number_format(in);
        ui_.scoreboard.scores[std::move(owner)][std::move(objective)] = std::move(score);
        return true;
    }
    if (packet.id == cb.reset_score) {
        auto& board = ui_.scoreboard;
        const std::string owner = in.read_string();
        if (in.read_bool()) {
            const std::string objective = in.read_string();
            if (const auto found = board.scores.find(owner); found != board.scores.end()) {
                found->second.erase(objective);
                if (found->second.empty()) board.scores.erase(found);
            }
        } else {
            board.scores.erase(owner);
        }
        return true;
    }
    if (packet.id == cb.set_player_team) {
        auto& board = ui_.scoreboard;
        const std::string name = in.read_string();
        const uint8_t method = in.read_u8();
        if (method == 1) {
            board.teams.erase(name);
            std::erase_if(board.team_of, [&](const auto& entry) { return entry.second == name; });
            return true;
        }
        if (method == 0 || method == 2) {
            auto& team = board.teams[name];
            team.display_name = nbt::read_network_tag(in);
            team.options = in.read_u8();
            team.nametag_visibility = in.read_varint();
            team.collision_rule = in.read_varint();
            team.colour = in.read_varint();
            team.prefix = nbt::read_network_tag(in);
            team.suffix = nbt::read_network_tag(in);
        }
        if (method == 0 || method == 3 || method == 4) {
            const int32_t count = in.read_varint();
            for (int32_t i = 0; i < count; ++i) {
                std::string member = in.read_string();
                if (method == 4) {
                    if (const auto found = board.team_of.find(member);
                        found != board.team_of.end() && found->second == name) {
                        board.team_of.erase(found);
                    }
                } else {
                    board.team_of[std::move(member)] = name;
                }
            }
        }
        return true;
    }
    return false;
}

namespace {

// The click packet's view of a stack: the item, the count and a hash per
// component. Without the components the hashes are left out, which the
// server reads as a mismatch for an item that has any, and answers by sending
// that slot again.
void write_hashed(ByteBuffer& out, const ItemStack& stack) {
    if (stack.id <= 0 || stack.count <= 0) {
        out.write_bool(false);
        return;
    }
    out.write_bool(true);
    out.write_varint(stack.id);
    out.write_varint(stack.count);
    out.write_varint(0);
    out.write_varint(0);
}

} // namespace

void Session::send_container_click(int32_t container, int16_t slot, int8_t button, int32_t mode,
                                   const std::vector<std::pair<int16_t, ItemStack>>& changed,
                                   const ItemStack& carried) {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_varint(container);
    out.write_varint(container != 0 && container == player_.open_container ? player_.open_state
                                                                           : player_.container_state);
    out.write_u16(static_cast<uint16_t>(slot));
    out.write_u8(static_cast<uint8_t>(button));
    out.write_varint(mode);
    out.write_varint(static_cast<int32_t>(std::min<size_t>(changed.size(), 128)));
    for (size_t i = 0; i < changed.size() && i < 128; ++i) {
        out.write_u16(static_cast<uint16_t>(changed[i].first));
        write_hashed(out, changed[i].second);
    }
    write_hashed(out, carried);
    send(ids_->play_sb.container_click, out);
}

void Session::send_container_close(int32_t container) {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_varint(container);
    send(ids_->play_sb.container_close, out);
    if (container == player_.open_container) {
        player_.open_container = 0;
        player_.open_slots.clear();
    }
}

void Session::predict_container(const std::vector<ItemStack>& slots, const ItemStack& carried) {
    if (player_.open_container == 0) return;
    player_.open_slots = slots;
    for (int32_t i = 0; i < static_cast<int32_t>(slots.size()); ++i) mirror_to_inventory(i);
    player_.inventory.carried = carried;
}

void Session::send_place_recipe(int32_t container, int32_t recipe, bool use_max) {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_varint(container);
    out.write_varint(recipe);
    out.write_bool(use_max);
    send(ids_->play_sb.place_recipe, out);
}

void Session::send_recipe_book_settings(int32_t book, bool open, bool filtering) {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_varint(book);
    out.write_bool(open);
    out.write_bool(filtering);
    send(ids_->play_sb.recipe_book_change_settings, out);
}

void Session::send_creative_slot(int16_t slot, const ItemStack& stack) {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_u16(static_cast<uint16_t>(slot));
    if (stack.id <= 0 || stack.count <= 0) {
        out.write_varint(0);
    } else {
        out.write_varint(stack.count);
        out.write_varint(stack.id);
        out.write_varint(0);
        out.write_varint(0);
    }
    send(ids_->play_sb.set_creative_mode_slot, out);
}

void Session::predict_inventory(const std::array<ItemStack, Inventory::kSlots>& slots,
                                const ItemStack& carried) {
    player_.inventory.slots = slots;
    player_.inventory.carried = carried;
}

void Session::send_respawn() {
    if (state_ != SessionState::Play) return;
    ByteBuffer out;
    out.write_varint(0);
    send(ids_->play_sb.client_command, out);
}

void Session::send_chat(std::string_view message) {
    if (state_ != SessionState::Play) throw ProtocolError("chat only works in the play state");

    ByteBuffer out;
    out.write_string(message);
    out.write_i64(now_millis());
    out.write_i64(0);          // salt
    out.write_bool(false);     // unsigned: needs enforce-secure-chat=false
    out.write_varint(0);       // message count
    for (size_t i = 0; i < kAcknowledgedBytes; ++i) out.write_u8(0);
    out.write_u8(0);           // checksum
    send(ids_->play_sb.chat, out);
}

void Session::send_command(std::string_view command) {
    if (state_ != SessionState::Play) throw ProtocolError("commands only work in the play state");

    ByteBuffer out;
    out.write_string(command);
    send(ids_->play_sb.chat_command, out);
}

namespace {

nbt::Value text_value(std::string text) {
    return nbt::Value(nbt::Value::Storage(std::move(text)));
}

// A chat type bound to its sender and target, decorating a message the way
// the game's own decoration does: the type's translation with the parameters
// it names, in its style.
nbt::Value decorate_chat(ByteBuffer& in, nbt::Value content, const Registries& registries) {
    std::string key = "chat.type.text";
    std::vector<int32_t> order = {0, 2};
    nbt::Value style;
    const int32_t holder = in.read_varint();
    if (holder == 0) {
        key = in.read_string();
        order.clear();
        const int32_t count = in.read_varint();
        for (int32_t i = 0; i < count; ++i) order.push_back(in.read_varint());
        style = nbt::read_network_tag(in);
        in.read_string();
        const int32_t narrated = in.read_varint();
        for (int32_t i = 0; i < narrated; ++i) in.read_varint();
        nbt::read_network_tag(in);
    } else if (const auto* entry = registries.at("minecraft:chat_type",
                                                 static_cast<size_t>(holder - 1));
               entry != nullptr && entry->data.has_value()) {
        if (const auto* chat = entry->data->find("chat")) {
            if (const auto* text = chat->find("translation_key"); text && text->as_string()) {
                key = *text->as_string();
            }
            if (const auto* list = chat->find("parameters"); list && list->as_list()) {
                order.clear();
                for (const auto& item : *list->as_list()) {
                    const std::string* name = item.as_string();
                    if (name == nullptr) continue;
                    order.push_back(*name == "sender" ? 0 : *name == "target" ? 1 : 2);
                }
            }
            if (const auto* found = chat->find("style")) style = *found;
        }
    }
    nbt::Value sender = nbt::read_network_tag(in);
    nbt::Value target = text_value("");
    if (in.read_bool()) target = nbt::read_network_tag(in);

    nbt::List with;
    for (const int32_t part : order) {
        with.push_back(part == 0 ? sender : part == 1 ? target : content);
    }
    nbt::Compound out;
    if (const auto* fields = style.as_compound()) out = *fields;
    out["translate"] = text_value(key);
    out["with"] = nbt::Value(nbt::Value::Storage(std::move(with)));
    return nbt::Value(nbt::Value::Storage(std::move(out)));
}

} // namespace

void Session::peek_chat_components(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    if (packet.id != cb.player_chat && packet.id != cb.disguised_chat &&
        packet.id != cb.system_chat) {
        return;
    }
    if (!events_.ui.on_chat_component && !events_.ui.on_overlay_message) return;
    auto& in = packet.data;
    const size_t start = in.position();
    try {
        if (packet.id == cb.system_chat) {
            const nbt::Value text = nbt::read_network_tag(in);
            if (in.read_bool()) {
                if (events_.ui.on_overlay_message) events_.ui.on_overlay_message(text);
            } else if (events_.ui.on_chat_component) {
                events_.ui.on_chat_component(text, 1);
            }
        } else if (packet.id == cb.disguised_chat) {
            nbt::Value body = nbt::read_network_tag(in);
            const nbt::Value line = decorate_chat(in, std::move(body), data_.registries);
            if (events_.ui.on_chat_component) events_.ui.on_chat_component(line, 1);
        } else {
            in.read_varint();
            in.read_bytes(detail::kUuidBytes);
            in.read_varint();
            const bool signed_message = in.read_bool();
            if (signed_message) in.read_bytes(kSignatureBytes);
            nbt::Value content = text_value(in.read_string(256));
            in.read_i64();
            in.read_i64();
            const int32_t previous = in.read_varint();
            for (int32_t i = 0; i < previous; ++i) {
                if (in.read_varint() == 0) in.read_bytes(kSignatureBytes);
            }
            if (in.read_bool()) content = nbt::read_network_tag(in);
            const int32_t filter = in.read_varint();
            if (filter == 2) {
                const int32_t longs = in.read_varint();
                for (int32_t i = 0; i < longs; ++i) in.read_i64();
            }
            const nbt::Value line = decorate_chat(in, std::move(content), data_.registries);
            if (filter != 1 && events_.ui.on_chat_component) {
                events_.ui.on_chat_component(line, signed_message ? 0 : 2);
            }
        }
    } catch (const std::exception&) {
    }
    in.set_position(start);
}

} // namespace mcpp
