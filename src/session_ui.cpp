#include "mcpp/session.hpp"
#include "session_detail.hpp"

#include "mcpp/json.hpp"
#include "mcpp/nbt.hpp"

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
    return false;
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
