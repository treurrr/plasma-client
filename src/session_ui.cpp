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

bool Session::handle_ui(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

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
        if (events_.ui.on_container_close) events_.ui.on_container_close(container);
        return true;
    }
    if (packet.id == cb.set_cursor_item) {
        // An item stack: count, then the item and its component patch. A
        // patch is not read here; the cursor keeps the id and count.
        ItemStack stack;
        stack.count = in.read_varint();
        if (stack.count > 0) {
            stack.id = in.read_varint();
            stack.readable = in.read_varint() == 0;
        } else {
            stack.count = 0;
        }
        player_.inventory.carried = stack;
        in.set_position(in.data().size());
        return true;
    }
    if (packet.id == cb.server_links) {
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
    out.write_varint(player_.container_state);
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

} // namespace mcpp
