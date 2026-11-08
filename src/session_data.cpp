#include "mcpp/session.hpp"

#include "session_detail.hpp"

#include <algorithm>
#include <format>

namespace mcpp {
namespace {

// Bits per biome before a section stores biome ids outright.
constexpr uint8_t kBiomeIndirectBits = 3;
constexpr size_t kCookieBytes = 5120;
constexpr int32_t kIntentTransfer = 3;

// set_player_inventory numbers the inventory itself: the hotbar, the three
// rows, feet to head, the off hand. The menu that container 0 describes
// starts with the crafting grid and puts the armour head first.
int32_t inventory_menu_slot(int32_t slot) {
    if (slot >= 0 && slot < 9) return static_cast<int32_t>(Inventory::kHotbarFirst) + slot;
    if (slot >= 9 && slot < 36) return slot;
    if (slot >= 36 && slot < 40) return 8 - (slot - 36);
    if (slot == 40) return static_cast<int32_t>(Inventory::kOffhand);
    return -1;
}

std::vector<uint8_t> rest_of(ByteBuffer& in) { return in.read_bytes(in.remaining()); }

} // namespace

void Session::read_tags(ByteBuffer& in) {
    const int32_t registries = in.read_varint();
    for (int32_t r = 0; r < registries; ++r) {
        std::string registry = in.read_string();
        Tags::Set set;
        const int32_t count = in.read_varint();
        for (int32_t t = 0; t < count; ++t) {
            std::string tag = in.read_string();
            const int32_t size = in.read_varint();
            if (size < 0) fail("negative tag size");
            std::vector<int32_t> ids(static_cast<size_t>(size));
            for (auto& id : ids) id = in.read_varint();
            set.emplace(std::move(tag), std::move(ids));
        }
        data_.tags.set(std::move(registry), std::move(set));
    }
}

const proto::Metadata& Session::metadata(int32_t entity) const {
    static const proto::Metadata none;
    if (entity == entity_id_) return data_.own_metadata;
    const auto found = entities_.entities.find(entity);
    return found == entities_.entities.end() ? none : found->second.metadata;
}

const std::map<int32_t, int32_t>& Session::container_properties(int32_t container) const {
    static const std::map<int32_t, int32_t> none;
    const auto found = data_.container_properties.find(container);
    return found == data_.container_properties.end() ? none : found->second;
}

float Session::cooldown_left(std::string_view group) const {
    const auto found = data_.cooldowns.find(group);
    if (found == data_.cooldowns.end() || found->second.ticks <= 0) return 0.0f;
    const std::chrono::duration<float> since =
        std::chrono::steady_clock::now() - found->second.start;
    const float ticks = since.count() * data_.tick_rate;
    return std::clamp(1.0f - ticks / static_cast<float>(found->second.ticks), 0.0f, 1.0f);
}

// The vanilla client names itself on the brand channel as soon as it is
// configured, before the server asks anything; so does this one.
void Session::send_brand() {
    if (data_.brand_sent) return;
    data_.brand_sent = true;
    ByteBuffer out;
    out.write_string("minecraft:brand");
    out.write_string("plasma");
    send(ids_->config_sb.custom_payload, out);
}

void Session::transfer(std::string host, uint16_t port) {
    if (playback_.is_open()) return;
    status(std::format("the server transfers us to {}:{}", host, port));
    connection_.socket().close();
    options_.host = std::move(host);
    options_.port = port;
    intent_ = kIntentTransfer;
    entities_ = EntityState{};
    data_.own_metadata = {};
    data_.container_properties.clear();
    data_.cooldowns.clear();
    handshake();
    run_login();
    begin_configuration();
    if (events_.data.on_transfer) events_.data.on_transfer(options_.host, options_.port);
}

bool Session::handle_data_common(PacketConnection::Packet& packet, bool configuration) {
    auto& in = packet.data;
    const int32_t id = packet.id;
    const auto& config = ids_->config_cb;
    const auto& play = ids_->play_cb;
    const auto is = [&](int32_t in_config, int32_t in_play) {
        return id == (configuration ? in_config : in_play);
    };

    if (is(config.custom_payload, play.custom_payload)) {
        const std::string channel = in.read_string();
        if (channel == "minecraft:brand") {
            data_.server_brand = in.read_string();
            return true;
        }
        const auto bytes = rest_of(in);
        if (events_.data.on_custom_payload) events_.data.on_custom_payload(channel, bytes);
        return true;
    }
    if (is(config.cookie_request, play.cookie_request)) {
        const std::string key = in.read_string();
        ByteBuffer out;
        out.write_string(key);
        const auto found = data_.cookies.find(key);
        out.write_bool(found != data_.cookies.end());
        if (found != data_.cookies.end()) {
            out.write_varint(static_cast<int32_t>(found->second.size()));
            out.write_bytes(found->second);
        }
        send(configuration ? ids_->config_sb.cookie_response : ids_->play_sb.cookie_response, out);
        return true;
    }
    if (is(config.store_cookie, play.store_cookie)) {
        std::string key = in.read_string();
        const int32_t size = in.read_varint();
        if (size < 0 || static_cast<size_t>(size) > kCookieBytes) {
            throw ProtocolError(std::format("a cookie of {} bytes", size));
        }
        data_.cookies[std::move(key)] = in.read_bytes(static_cast<size_t>(size));
        return true;
    }
    if (is(config.transfer, play.transfer)) {
        std::string host = in.read_string();
        const int32_t port = in.read_varint();
        transfer(std::move(host), static_cast<uint16_t>(port));
        return true;
    }
    // R: `ClientboundResourcePackPushPacket` and `ClientboundResourcePackPopPacket`.
    // Without a handler every pack is declined, which is what this client did
    // before it could load one at all.
    if (is(config.resource_pack_push, play.resource_pack_push)) {
        packs::Push push;
        const auto pack_id = in.read_bytes(detail::kUuidBytes);
        std::copy(pack_id.begin(), pack_id.end(), push.id.begin());
        push.url = in.read_string();
        push.hash = in.read_string(packs::kMaxHashLength);
        push.required = in.read_bool();
        if (in.read_bool()) push.prompt = detail::read_component(in, lookup_);
        if (events_.packs.on_push) {
            events_.packs.on_push(push);
        } else {
            send_resource_pack(push.id, packs::Action::Declined);
        }
        return true;
    }
    if (is(config.resource_pack_pop, play.resource_pack_pop)) {
        packs::Pop pop;
        if (in.read_bool()) {
            const auto pack_id = in.read_bytes(detail::kUuidBytes);
            packs::PackId value{};
            std::copy(pack_id.begin(), pack_id.end(), value.begin());
            pop.id = value;
        }
        if (events_.packs.on_pop) events_.packs.on_pop(pop);
        return true;
    }
    if (is(config.custom_report_details, play.custom_report_details)) {
        data_.report_details = proto::read_named(in, "custom_report_details", options_.protocol);
        return true;
    }
    if (is(config.server_links, play.server_links)) {
        data_.server_links = proto::read_named(in, "server_links", options_.protocol);
        return true;
    }
    if (configuration && id == config.update_enabled_features) {
        const int32_t count = in.read_varint();
        data_.features.clear();
        for (int32_t i = 0; i < count; ++i) data_.features.push_back(in.read_string());
        return true;
    }
    if (configuration && id == config.reset_chat) {
        if (events_.data.on_reset_chat) events_.data.on_reset_chat();
        return true;
    }
    return false;
}

bool Session::handle_data(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    // A decoder that meets a layout it does not know cannot find the rest of
    // the packet. That is said once per packet type, and the session goes on.
    try {
        if (handle_data_common(packet, false)) return true;

        if (packet.id == cb.change_difficulty) {
            data_.difficulty = in.read_u8();
            data_.difficulty_locked = in.read_bool();
            return true;
        }
        if (packet.id == cb.update_tags) {
            read_tags(in);
            return true;
        }
        if (packet.id == cb.set_entity_data) {
            const int32_t entity = in.read_varint();
            const proto::Metadata update = proto::read_metadata(in, options_.protocol);
            if (entity == entity_id_) {
                data_.own_metadata.merge(update);
            } else if (const auto found = entities_.entities.find(entity);
                       found != entities_.entities.end()) {
                found->second.metadata.merge(update);
            }
            if (events_.data.on_entity_data) events_.data.on_entity_data(entity);
            return true;
        }
        if (packet.id == cb.set_cursor_item) {
            player_.inventory.carried = proto::read_item_stack(in, options_.protocol);
            return true;
        }
        if (packet.id == cb.set_player_inventory) {
            const int32_t slot = inventory_menu_slot(in.read_varint());
            ItemStack stack = proto::read_item_stack(in, options_.protocol);
            if (slot >= 0) {
                player_.inventory.slots[static_cast<size_t>(slot)] = std::move(stack);
                mirror_to_container(slot);
            }
            return true;
        }
        if (packet.id == cb.container_set_data) {
            const int32_t container = in.read_varint();
            const auto property = static_cast<int16_t>(in.read_u16());
            const auto value = static_cast<int16_t>(in.read_u16());
            data_.container_properties[container][property] = value;
            return true;
        }
        if (packet.id == cb.commands) {
            data_.commands = proto::read_commands(in, options_.protocol);
            return true;
        }
        if (packet.id == cb.update_recipes) {
            data_.recipe_sets = proto::read_named(in, "update_recipes", options_.protocol);
            ++data_.recipe_changes;
            return true;
        }
        if (packet.id == cb.recipe_book_add) {
            proto::Value added = proto::read_named(in, "recipe_book_add", options_.protocol);
            const auto* replace = added.field("replace");
            if (replace != nullptr && replace->as_bool().value_or(false)) data_.recipe_book.clear();
            if (events_.ui.on_recipe_book_add) events_.ui.on_recipe_book_add(added);
            data_.recipe_book.push_back(std::move(added));
            ++data_.recipe_changes;
            return true;
        }
        if (packet.id == cb.recipe_book_remove) {
            const int32_t count = in.read_varint();
            std::vector<int64_t> gone;
            for (int32_t i = 0; i < count; ++i) gone.push_back(in.read_varint());
            if (events_.ui.on_recipe_book_remove) {
                events_.ui.on_recipe_book_remove(std::vector<int32_t>(gone.begin(), gone.end()));
            }
            for (auto& batch : data_.recipe_book) {
                const auto* entries = batch.field("entries");
                if (entries == nullptr || entries->as_list() == nullptr) continue;
                proto::Value::List kept;
                for (const auto& entry : *entries->as_list()) {
                    const auto* number = entry.field("id");
                    const int64_t recipe = number != nullptr ? number->as_int().value_or(-1) : -1;
                    if (std::find(gone.begin(), gone.end(), recipe) == gone.end()) {
                        kept.push_back(entry);
                    }
                }
                proto::Value::Record record{{"entries", proto::Value{std::move(kept)}}};
                if (const auto* replace = batch.field("replace")) record.push_back({"replace", *replace});
                batch = proto::Value{std::move(record)};
            }
            ++data_.recipe_changes;
            return true;
        }
        if (packet.id == cb.recipe_book_settings) {
            data_.recipe_book_settings =
                proto::read_named(in, "recipe_book_settings", options_.protocol);
            ++data_.recipe_changes;
            if (events_.ui.on_recipe_book_settings) {
                events_.ui.on_recipe_book_settings(data_.recipe_book_settings);
            }
            return true;
        }
        if (packet.id == cb.server_data) {
            data_.server_data = proto::read_named(in, "server_data", options_.protocol);
            return true;
        }
        if (packet.id == cb.ticking_state) {
            data_.tick_rate = in.read_f32();
            data_.tick_frozen = in.read_bool();
            return true;
        }
        if (packet.id == cb.ticking_step) {
            data_.tick_steps = in.read_varint();
            return true;
        }
        if (packet.id == cb.cooldown) {
            std::string group = in.read_string();
            const int32_t ticks = in.read_varint();
            if (ticks <= 0) {
                data_.cooldowns.erase(group);
            } else {
                data_.cooldowns[std::move(group)] = {std::chrono::steady_clock::now(), ticks};
            }
            return true;
        }
        if (packet.id == cb.chunks_biomes) {
            const int32_t chunks = in.read_varint();
            for (int32_t c = 0; c < chunks; ++c) {
                // One big-endian long with z in the high half.
                const int32_t z = in.read_i32();
                const int32_t x = in.read_i32();
                const int32_t size = in.read_varint();
                if (size < 0 || static_cast<size_t>(size) > in.remaining()) {
                    throw ProtocolError(std::format("chunk biomes of {} bytes", size));
                }
                ByteBuffer body(in.read_bytes(static_cast<size_t>(size)));
                std::vector<world::PalettedContainer> sections;
                while (body.remaining() != 0) {
                    sections.push_back(world::PalettedContainer::read(
                        body, world::kBiomesPerSection, kBiomeIndirectBits));
                }
                if (events_.data.on_chunk_biomes) events_.data.on_chunk_biomes(x, z, sections);
            }
            return true;
        }
    } catch (const ProtocolError& error) {
        if (data_.warned.insert(packet.id).second) {
            status(std::format("packet {} not decoded: {}", packet.id, error.what()));
        }
        return true;
    }
    return false;
}

// `ServerboundResourcePackPacket`: the same answer in either state, under the
// id that state gives it.
void Session::send_resource_pack(const packs::PackId& id, packs::Action action) {
    ByteBuffer out;
    out.write_bytes(id);
    out.write_varint(static_cast<int32_t>(action));
    send(state_ == SessionState::Configuration ? ids_->config_sb.resource_pack
                                               : ids_->play_sb.resource_pack,
         out);
}

} // namespace mcpp
