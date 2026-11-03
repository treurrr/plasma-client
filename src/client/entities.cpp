#include "client/game.hpp"

#include <format>
#include <iostream>

namespace client {

void entities_bind(Game&, mcpp::SessionEvents& events) {
    events.entities.on_add = [](const mcpp::Entity& entity) {
        std::cout << std::format("- entity {} of type {} appeared at {:.2f} {:.2f} {:.2f}\n",
                                 entity.id, entity.type, entity.x, entity.y, entity.z);
    };
    events.entities.on_remove = [](int32_t id) {
        std::cout << std::format("- entity {} gone\n", id);
    };
}

void entities_start(Game&) {}
void entities_input(Game&) {}
void entities_tick(Game&) {}
void entities_draw(Game&) {}
void entities_stop(Game&) {}

} // namespace client
