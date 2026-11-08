// M: the window with no window. Everything the client asks of the window
// other than a surface - the keys, the clicks, the pointer, the raw events and
// the clock the double taps read - has to behave the same whether a hand or a
// script is driving it, or a headless session proves nothing.
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/window.hpp"
#include "mcpp/keybinds.hpp"

#include <GLFW/glfw3.h>

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

mcpp::gpu::Device& device() {
    // Never created: a headless window touches no Vulkan at all.
    static mcpp::gpu::Device only;
    return only;
}

} // namespace

int main() {
    using mcpp::gpu::Window;

    {
        Window window(device(), 854, 480, Window::Headless{});
        check(window.headless(), "a headless window says so");
        check(window.open(), "and starts open");
        check(window.width() == 854 && window.height() == 480, "with the size it was given");
        check(window.content_scale() == 1.0, "and one framebuffer pixel per gui pixel");

        const auto idle = window.poll();
        check(idle.forward == 0.0f && idle.right == 0.0f && idle.up == 0.0f,
              "nothing is held before anything is pressed");

        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        const auto walking = window.poll();
        check(walking.forward == 1.0f, "W walks forward");
        check(window.key_down(GLFW_KEY_W), "and reads as held");
        window.send_key(GLFW_KEY_W, GLFW_RELEASE);
        check(window.poll().forward == 0.0f, "letting go stops");

        window.send_key(GLFW_KEY_S, GLFW_PRESS);
        window.send_key(GLFW_KEY_A, GLFW_PRESS);
        check(window.poll().forward == -1.0f, "S walks back");
        window.send_key(GLFW_KEY_S, GLFW_RELEASE);
        window.send_key(GLFW_KEY_A, GLFW_RELEASE);
        window.poll();

        window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        const auto hitting = window.poll();
        check(hitting.attack, "the left button attacks while it is down");
        check(hitting.click, "and counts as one press on the frame it goes down");
        check(!window.poll().click, "but not again while it stays down");
        window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        check(!window.poll().attack, "and stops when it comes up");

        window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
        check(window.poll().use, "the right button uses");
        window.send_button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
        window.poll();
        check(window.clicks_per_second(false) == 1, "one left click in the last second");
        check(window.clicks_per_second(true) == 1, "and one right");
    }

    {
        // The pointer: a move is a look while the cursor is captured, and the
        // first frame after a capture change is not a jump.
        Window window(device(), 854, 480, Window::Headless{});
        window.send_cursor(100.0, 100.0);
        window.poll();
        window.send_cursor(140.0, 90.0);
        const auto turned = window.poll();
        check(turned.yaw_delta == 40.0f && turned.pitch_delta == -10.0f,
              "the pointer's move is the frame's look");
        check(turned.cursor_x == 140.0f && turned.cursor_y == 90.0f, "and where it now is");
        window.capture_cursor(false);
        window.send_cursor(400.0, 300.0);
        const auto freed = window.poll();
        check(freed.yaw_delta == 0.0f && freed.pitch_delta == 0.0f,
              "a free pointer does not turn the view");
    }

    {
        // The clock the double taps read is the script's, so a double tap can
        // be timed exactly.
        Window window(device(), 854, 480, Window::Headless{});
        window.poll();
        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(!window.poll().sprint, "one tap forward does not sprint");
        window.send_key(GLFW_KEY_W, GLFW_RELEASE);
        window.poll();
        window.advance_clock(0.1);
        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(window.poll().sprint, "two taps inside 300 ms do");
        window.send_key(GLFW_KEY_W, GLFW_RELEASE);
        window.poll();
        window.advance_clock(1.0);
        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(!window.poll().sprint, "two taps a second apart do not");
    }

    {
        // The raw events, in the order they were sent: a screen needs them
        // one at a time, not as a state.
        Window window(device(), 854, 480, Window::Headless{});
        window.send_key(GLFW_KEY_E, GLFW_PRESS);
        window.send_char('x');
        window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        window.send_scroll(-1.0);
        window.send_cursor(10.0, 20.0);
        const auto events = window.take_events();
        using Type = mcpp::gpu::InputEvent::Type;
        check(events.size() == 5, "every event is kept");
        if (events.size() == 5) {
            check(events[0].type == Type::Key && events[0].key == GLFW_KEY_E &&
                      events[0].action == GLFW_PRESS,
                  "the key first");
            check(events[1].type == Type::Char && events[1].key == 'x', "then the character");
            check(events[2].type == Type::Button && events[2].action == GLFW_PRESS,
                  "then the button");
            check(events[3].type == Type::Scroll && events[3].y == -1.0, "then the wheel");
            check(events[4].type == Type::Move && events[4].x == 10.0 && events[4].y == 20.0,
                  "then the move");
        }
        check(window.take_events().empty(), "and taken events are gone");

        window.send_cursor(11.0, 21.0);
        window.send_cursor(12.0, 22.0);
        const auto moves = window.take_events();
        check(moves.size() == 1 && moves[0].x == 12.0,
              "two moves in one frame collapse into the last");
    }

    {
        // While typing, the keyboard belongs to the line, and the clipboard is
        // the window's.
        Window window(device(), 854, 480, Window::Headless{});
        window.begin_typing();
        check(window.typing(), "typing begins");
        window.send_char('h');
        window.send_char('i');
        check(window.typed() == "hi", "the characters land on the line");
        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(window.poll().forward == 0.0f, "and W does not walk while typing");
        window.send_key(GLFW_KEY_BACKSPACE, GLFW_PRESS);
        window.poll();
        check(window.typed() == "h", "backspace takes one off");
        window.send_key(GLFW_KEY_ENTER, GLFW_PRESS);
        check(window.poll().submit_text, "enter submits");
        window.end_typing();
        check(!window.typing() && window.typed().empty(), "and the line is let go");

        window.set_clipboard("pasted");
        check(window.clipboard() == "pasted", "the clipboard keeps what was put on it");
    }

    {
        // Every field of a frame's input means its binding, not a fixed key.
        // The game's defaults are the keys that were hard-coded, so a default
        // install sees no change; a rebound key is the whole point.
        mcpp::Keybinds binds;
        Window window(device(), 854, 480, Window::Headless{});
        window.set_keybinds(&binds);

        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(window.poll().forward == 1.0f, "the default forward key still walks");
        window.send_key(GLFW_KEY_W, GLFW_RELEASE);
        window.poll();

        binds.find("key.forward")->current = mcpp::InputKey::keyboard(GLFW_KEY_K);
        window.send_key(GLFW_KEY_W, GLFW_PRESS);
        check(window.poll().forward == 0.0f, "the old key stops walking once it is rebound");
        window.send_key(GLFW_KEY_W, GLFW_RELEASE);
        window.send_key(GLFW_KEY_K, GLFW_PRESS);
        check(window.poll().forward == 1.0f, "and the new one walks");
        window.send_key(GLFW_KEY_K, GLFW_RELEASE);
        window.poll();

        // A binding on a mouse button, and one on nothing at all.
        binds.find("key.attack")->current = mcpp::InputKey::keyboard(GLFW_KEY_Z);
        window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        check(!window.poll().attack, "the left button stops attacking once attack is on a key");
        window.send_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        window.send_key(GLFW_KEY_Z, GLFW_PRESS);
        const auto hit = window.poll();
        check(hit.attack && hit.attack_clicks == 1, "and the key attacks, once per press");
        check(window.poll().attack_clicks == 0, "and not again while it is held");
        window.send_key(GLFW_KEY_Z, GLFW_RELEASE);
        window.poll();

        binds.find("key.jump")->current = {};
        window.send_key(GLFW_KEY_SPACE, GLFW_PRESS);
        check(window.poll().up == 0.0f, "a binding on nothing is never held");
        window.send_key(GLFW_KEY_SPACE, GLFW_RELEASE);
        window.poll();

        // The three the client never read at all: drop, swap and pick.
        binds.find("key.drop")->current = mcpp::InputKey::keyboard(GLFW_KEY_G);
        window.send_key(GLFW_KEY_G, GLFW_PRESS);
        const auto dropped = window.poll();
        check(dropped.drop && !dropped.drop_stack, "the drop binding drops one");
        window.send_key(GLFW_KEY_G, GLFW_RELEASE);
        window.send_key(GLFW_KEY_LEFT_CONTROL, GLFW_PRESS);
        window.poll();
        window.send_key(GLFW_KEY_G, GLFW_PRESS);
        check(window.poll().drop_stack, "and with control held, the whole stack");
        window.send_key(GLFW_KEY_G, GLFW_RELEASE);
        window.send_key(GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE);
        window.poll();

        window.send_key(GLFW_KEY_F, GLFW_PRESS);
        check(window.poll().swap_offhand, "F swaps to the offhand");
        window.send_key(GLFW_KEY_F, GLFW_RELEASE);
        window.poll();

        window.send_button(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS);
        check(window.poll().pick_item_clicks == 1, "the middle button picks the block");
        window.send_button(GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE);
        window.poll();

        binds.find("key.hotbar.4")->current = mcpp::InputKey::keyboard(GLFW_KEY_P);
        window.send_key(GLFW_KEY_P, GLFW_PRESS);
        check(window.poll().pick_slot == 3, "a rebound hotbar key picks its own slot");
        window.send_key(GLFW_KEY_P, GLFW_RELEASE);
        window.poll();
    }

    {
        Window window(device(), 854, 480, Window::Headless{});
        check(window.open(), "the window is open");
        window.send_close();
        check(!window.open(), "and closes when the script says so");
    }

    if (failures == 0) std::cout << "test_window ok\n";
    return failures == 0 ? 0 : 1;
}
