#include "client/hand_view.hpp"

#include "mcpp/gpu/hand.hpp"

#include <cstring>

// The step between the game's poses and the gpu. It needs neither a window
// nor a game behind it, so a shot and the window itself run the same one.
namespace client {

void queue_hand(mcpp::gpu::HandRenderer& renderer, const mcpp::client::HandPlayer& player,
                const mcpp::client::HandBob& bob, bool slim) {
    mcpp::client::HandDraw draws[2];
    int count = 0;
    mcpp::client::hands_with_items(player, bob, draws, count);
    renderer.begin();
    for (int i = 0; i < count; ++i) {
        const auto& draw = draws[i];
        if (draw.is_arm) {
            renderer.add_arm(draw.pose, draw.arm == mcpp::client::Arm::Left, slim, true);
            continue;
        }
        const bool left = draw.arm == mcpp::client::Arm::Left;
        const auto display = renderer.item_display(draw.item, left);
        mcpp::client::Pose pose;
        pose.matrix = draw.pose;
        pose.identity = false;
        pose.moved_only = false;
        mcpp::client::ItemDisplay transform;
        std::memcpy(transform.rotation, display.rotation, sizeof(transform.rotation));
        std::memcpy(transform.translation, display.translation, sizeof(transform.translation));
        std::memcpy(transform.scale, display.scale, sizeof(transform.scale));
        transform.present = display.present;
        mcpp::client::apply_display_transform(pose, transform, left);
        renderer.add_item(draw.item, pose.matrix, draw.pull);
    }
}

} // namespace client
