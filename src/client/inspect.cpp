#include "client/inspect.hpp"
#include "client/common.hpp"

#include "mcpp/proto/components.hpp"
#include "mcpp/proto/metadata.hpp"
#include "mcpp/session.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <optional>

namespace client {
namespace {

void print_registries(const mcpp::Session& session) {
    const auto& registries = session.registries();
    size_t with_bodies = 0;
    size_t entries = 0;
    for (const auto& [name, list] : registries.all()) {
        entries += list.size();
        for (const auto& entry : list) {
            if (entry.data.has_value()) ++with_bodies;
        }
    }
    std::cout << std::format("{} registries, {} entries, {} of them with a body\n",
                             registries.size(), entries, with_bodies);
    for (const auto& [name, list] : registries.all()) {
        std::cout << std::format("  {:5}  {}\n", list.size(), name);
    }
    if (const auto* types = registries.find("minecraft:chat_type")) {
        for (const auto& entry : *types) {
            if (!entry.data.has_value()) continue;
            const auto* chat = entry.data->find("chat");
            const auto* key = chat != nullptr ? chat->find("translation_key") : nullptr;
            std::string parameters;
            if (const auto* list = chat != nullptr ? chat->find("parameters") : nullptr) {
                if (const auto* items = list->as_list()) {
                    for (const auto& item : *items) {
                        if (const auto* text = item.as_string()) parameters += *text + " ";
                    }
                }
            }
            std::cout << std::format("  chat {:28} {:34} {}\n", entry.name,
                                     key && key->as_string() ? *key->as_string() : "?", parameters);
        }
    }
    if (const auto* types = registries.find("minecraft:dimension_type")) {
        for (const auto& entry : *types) {
            if (!entry.data.has_value()) continue;
            const auto number = [&](std::string_view key) {
                const auto* value = entry.data->find(key);
                return value != nullptr ? value->as_int().value_or(0) : 0;
            };
            std::cout << std::format("  dimension {:28} min_y {:4}  height {:3}  sky {}  ceiling {}\n",
                                     entry.name, number("min_y"), number("height"),
                                     number("has_skylight"), number("has_ceiling"));
        }
    }
    const auto& dimension = session.dimension();
    std::cout << std::format("  standing in {} ({}), {} sections from y {}\n", dimension.name,
                             dimension.type, dimension.section_count(), dimension.min_y);
}

} // namespace

int inspect(const std::string& target, const std::string& username, double seconds,
            const std::string& record) {
    mcpp::SessionOptions options;
    if (target.starts_with("replay:")) {
        options.replay = target.substr(7);
    } else {
        const auto address = parse_address(target);
        options.host = address.host;
        options.port = address.port;
    }
    std::string name = username;
    if (options.replay.empty()) options.profile = signed_in_profile(name);
    options.username = name;
    options.record = record;
    std::optional<std::chrono::steady_clock::time_point> in_world;
    mcpp::SessionEvents events;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    // Light is the renderer's; inspect skips it whole rather than leave it half read.
    events.world.on_light_update = [](int32_t, int32_t, mcpp::ByteBuffer& light) {
        light.read_bytes(light.remaining());
    };
    events.player.on_position = [&](double, double, double, float, float) {
        if (!in_world) in_world = std::chrono::steady_clock::now();
    };
    mcpp::Session session(options, events);
    session.connect();
    const auto started = std::chrono::steady_clock::now();
    const auto budget = std::chrono::duration<double>(seconds);
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (in_world && options.replay.empty() && now - *in_world > budget) break;
        if (!in_world && now - started > budget + std::chrono::seconds(10)) break;
        try {
            if (!session.pump()) break;
        } catch (const mcpp::TimeoutError&) {
            break;
        }
    }
    session.close();

    print_registries(session);

    const auto& registries = session.registries();
    const int32_t protocol = options.protocol;
    const mcpp::proto::RegistryLookup name_of = [&](std::string_view registry, int64_t id) {
        if (registry == "minecraft:data_component_type") {
            return std::string(mcpp::proto::component_name(static_cast<int32_t>(id), protocol));
        }
        const auto* entry = registries.at(registry, static_cast<size_t>(id));
        return entry != nullptr ? entry->name : std::string{};
    };

    const auto& inventory = session.inventory();
    std::cout << "inventory\n";
    for (size_t i = 0; i < inventory.slots.size(); ++i) {
        if (inventory.slots[i].count == 0 && inventory.slots[i].readable) continue;
        std::cout << std::format("  slot {:2}  {}\n", i,
                                 mcpp::proto::describe(inventory.slots[i], name_of));
    }
    if (inventory.carried.count != 0) {
        std::cout << std::format("  carried  {}\n", mcpp::proto::describe(inventory.carried, name_of));
    }

    const auto print_metadata = [&](const mcpp::proto::Metadata& metadata, std::string_view type) {
        for (const auto& entry : metadata.entries()) {
            const auto* field = mcpp::proto::metadata_field(type, entry.index);
            std::string note;
            if (field == nullptr) {
                note = "  NOT IN TABLE";
            } else if (field->type != entry.type) {
                note = std::format("  TABLE SAYS {}", mcpp::proto::meta_type_name(field->type));
            }
            std::cout << std::format("      {:2} {:28} {:22} {}{}\n", entry.index,
                                     field != nullptr ? field->name : "?",
                                     mcpp::proto::meta_type_name(entry.type),
                                     mcpp::proto::describe(entry.value, name_of), note);
        }
    };
    std::cout << std::format("own data\n");
    print_metadata(session.metadata(session.entity_id()), "minecraft:player");
    std::cout << std::format("{} entities\n", session.entity_count());
    for (const auto& [id, entity] : session.entities()) {
        const std::string type = name_of("minecraft:entity_type", entity.type);
        std::cout << std::format("  {:6} {:28} {:9.2f} {:7.2f} {:9.2f}\n", id, type, entity.x,
                                 entity.y, entity.z);
        print_metadata(entity.metadata, type);
    }

    const auto& commands = session.commands();
    size_t arguments = 0;
    for (const auto& node : commands.nodes) {
        if (node.kind == mcpp::proto::CommandNode::Kind::Argument) ++arguments;
    }
    const auto* root = commands.root_node();
    std::cout << std::format("commands: {} nodes, {} arguments, {} top-level\n",
                             commands.nodes.size(), arguments,
                             root != nullptr ? root->children.size() : 0);
    size_t recipes = 0;
    for (const auto& batch : session.recipe_book()) {
        if (const auto* entries = batch.field("entries"); entries && entries->as_list()) {
            recipes += entries->as_list()->size();
        }
    }
    const auto* sets = session.recipe_sets().field("property_sets");
    const auto* stonecutter = session.recipe_sets().field("stonecutter");
    std::cout << std::format("recipes: {} in the book, {} property sets, {} stonecutter\n",
                             recipes, sets && sets->as_list() ? sets->as_list()->size() : 0,
                             stonecutter && stonecutter->as_list() ? stonecutter->as_list()->size()
                                                                  : 0);
    std::cout << std::format("recipe book settings: {}\n",
                             mcpp::proto::describe(session.recipe_book_settings(), name_of));
    std::cout << std::format("server brand: {}\n", session.server_brand());
    std::cout << std::format("server data: {}\n",
                             mcpp::proto::describe(session.server_data(), name_of));
    std::cout << std::format("server links: {}\n",
                             mcpp::proto::describe(session.server_links(), name_of));
    std::cout << std::format("report details: {}\n",
                             mcpp::proto::describe(session.report_details(), name_of));
    std::string features;
    for (const auto& feature : session.enabled_features()) features += feature + " ";
    std::cout << std::format("features: {}\n", features);
    std::cout << std::format("ticks: {} per second{}\n", session.tick_rate(),
                             session.tick_frozen() ? ", frozen" : "");
    return 0;
}

} // namespace client
