#include "mcpp/gpu/window.hpp"

#include "mcpp/keybinds.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <format>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace mcpp::gpu {

namespace {

constexpr size_t kFramesInFlight = 2;

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("{} failed: {}", what, static_cast<int>(result)));
    }
}

void transition(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
                VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

bool glfw_ready = false;

void ensure_glfw() {
    if (glfw_ready) return;
    if (glfwInit() != GLFW_TRUE) throw std::runtime_error("glfwInit failed");
    glfw_ready = true;
}

} // namespace

std::vector<const char*> Window::required_instance_extensions() {
    ensure_glfw();
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw std::runtime_error("no Vulkan loader for the window system");
    }
    uint32_t count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr) throw std::runtime_error("no surface extensions for this platform");
    return std::vector<const char*>(names, names + count);
}

Window::Window(Device& device, uint32_t width, uint32_t height, const std::string& title)
    : device_(device) {
    ensure_glfw();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(),
                               nullptr, nullptr);
    if (window_ == nullptr) throw std::runtime_error("could not create a window");

    glfwSetWindowUserPointer(window_, this);
    glfwSetCharCallback(window_, [](GLFWwindow* handle, unsigned int code) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) return;
        self->events_.push_back({InputEvent::Type::Char, static_cast<int>(code), 0, 0, 0, 0.0, 0.0});
        if (!self->typing_) return;
        if (code >= 32 && code < 127 && self->typed_.size() < 256) {
            self->typed_ += static_cast<char>(code);
        }
    });

    glfwSetScrollCallback(window_, [](GLFWwindow* handle, double x, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) return;
        self->wheel_ += y;
        self->events_.push_back({InputEvent::Type::Scroll, 0, 0, 0, 0, x, y});
    });
    glfwSetKeyCallback(window_, [](GLFWwindow* handle, int key, int scancode, int action,
                                   int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) return;
        self->events_.push_back({InputEvent::Type::Key, key, scancode, action, mods, 0.0, 0.0});
    });
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* handle, int button, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) return;
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(handle, &x, &y);
        self->events_.push_back({InputEvent::Type::Button, button, 0, action, mods, x, y});
    });
    glfwSetCursorPosCallback(window_, [](GLFWwindow* handle, double x, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) return;
        // Moves collapse into one per frame: only the last position matters.
        if (!self->events_.empty() && self->events_.back().type == InputEvent::Type::Move) {
            self->events_.back().x = x;
            self->events_.back().y = y;
            return;
        }
        self->events_.push_back({InputEvent::Type::Move, 0, 0, 0, 0, x, y});
    });
    // Wayland never reports a resized surface as out of date, so a tiling
    // compositor would otherwise get the first swapchain stretched to fit.
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* handle, int, int) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr) self->swapchain_stale_ = true;
    });

    check(glfwCreateWindowSurface(device_.instance(), window_, nullptr, &surface_),
          "glfwCreateWindowSurface");

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device_.physical(), device_.graphics_family(), surface_,
                                         &supported);
    if (supported != VK_TRUE) {
        throw std::runtime_error("the graphics queue cannot present to this surface");
    }

    VkCommandPoolCreateInfo pool_create{};
    pool_create.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_create.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_create.queueFamilyIndex = device_.graphics_family();
    check(vkCreateCommandPool(device_.handle(), &pool_create, nullptr, &pool_), "vkCreateCommandPool");

    frames_.resize(kFramesInFlight);
    for (auto& frame : frames_) {
        VkCommandBufferAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = pool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_.handle(), &allocate, &frame.command),
              "vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device_.handle(), &semaphore, nullptr, &frame.acquired),
              "vkCreateSemaphore");
        check(vkCreateSemaphore(device_.handle(), &semaphore, nullptr, &frame.rendered),
              "vkCreateSemaphore");

        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_.handle(), &fence, nullptr, &frame.in_flight), "vkCreateFence");
    }

    if (const char* vsync = std::getenv("PLASMA_VSYNC")) vsync_ = std::string_view(vsync) != "0";
    create_swapchain();

    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

// M: the headless window. Nothing is created that a screen would need, so
// there is no glfw, no surface and no swapchain to go stale; the colour format
// is the one WorldRenderer draws into, which is where the overlay lands.
Window::Window(Device& device, uint32_t width, uint32_t height, Headless)
    : device_(device) {
    headless_ = true;
    format_ = VK_FORMAT_R8G8B8A8_UNORM;
    extent_ = {width, height};
    cursor_captured_ = true;
}

void Window::send_key(int key, int action, int mods) {
    held_[key] = action != GLFW_RELEASE;
    events_.push_back({InputEvent::Type::Key, key, 0, action, mods, 0.0, 0.0});
}

void Window::send_char(uint32_t code) {
    events_.push_back({InputEvent::Type::Char, static_cast<int>(code), 0, 0, 0, 0.0, 0.0});
    if (!typing_) return;
    if (code >= 32 && code < 127 && typed_.size() < 256) typed_ += static_cast<char>(code);
}

void Window::send_button(int button, int action, int mods) {
    if (button >= 0 && button < 8) held_buttons_[button] = action != GLFW_RELEASE;
    events_.push_back({InputEvent::Type::Button, button, 0, action, mods, last_x_, last_y_});
}

void Window::send_scroll(double y) {
    wheel_ += y;
    events_.push_back({InputEvent::Type::Scroll, 0, 0, 0, 0, 0.0, y});
}

void Window::send_cursor(double x, double y) {
    if (!events_.empty() && events_.back().type == InputEvent::Type::Move) {
        events_.back().x = x;
        events_.back().y = y;
    } else {
        events_.push_back({InputEvent::Type::Move, 0, 0, 0, 0, x, y});
    }
    pending_x_ = x;
    pending_y_ = y;
}

void Window::send_close() { closed_ = true; }

void Window::advance_clock(double seconds) { clock_ += seconds; }

Window::~Window() {
    if (headless_) return;
    if (device_.handle() != VK_NULL_HANDLE) vkDeviceWaitIdle(device_.handle());
    destroy_swapchain();
    for (auto& frame : frames_) {
        if (frame.acquired != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), frame.acquired, nullptr);
        if (frame.rendered != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), frame.rendered, nullptr);
        if (frame.in_flight != VK_NULL_HANDLE) vkDestroyFence(device_.handle(), frame.in_flight, nullptr);
    }
    if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_.handle(), pool_, nullptr);
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(device_.instance(), surface_, nullptr);
    if (window_ != nullptr) glfwDestroyWindow(window_);
}

void Window::create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physical(), surface_, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_.physical(), surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_.physical(), surface_, &format_count, formats.data());

    VkSurfaceFormatKHR chosen = formats.front();
    for (const auto& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    }
    format_ = chosen.format;

    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        extent_.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,
                                   caps.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height,
                                    caps.maxImageExtent.height);
    }

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) count = std::min(count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR create{};
    create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create.surface = surface_;
    create.minImageCount = count;
    create.imageFormat = format_;
    create.imageColorSpace = chosen.colorSpace;
    create.imageExtent = extent_;
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.preTransform = caps.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // FIFO is the only mode every driver must offer. Without vsync, mailbox
    // replaces a waiting frame with a newer one instead of tearing; immediate
    // is the last resort.
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_) {
        uint32_t mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device_.physical(), surface_, &mode_count,
                                                  nullptr);
        std::vector<VkPresentModeKHR> modes(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device_.physical(), surface_, &mode_count,
                                                  modes.data());
        for (const VkPresentModeKHR wanted :
             {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::ranges::find(modes, wanted) != modes.end()) {
                mode = wanted;
                break;
            }
        }
    }
    create.presentMode = mode;
    create.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_.handle(), &create, nullptr, &swapchain_),
          "vkCreateSwapchainKHR");

    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &image_count, nullptr);
    images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &image_count, images_.data());

    views_.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = images_[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device_.handle(), &view, nullptr, &views_[i]),
              "vkCreateImageView");
    }

    // One per image, not per frame in flight: a semaphore signalled by a present
    // may not be reused until that image comes back.
    release_.resize(image_count);
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& signal : release_) {
        check(vkCreateSemaphore(device_.handle(), &semaphore, nullptr, &signal),
              "vkCreateSemaphore");
    }
}

void Window::destroy_swapchain() {
    for (auto& view : views_) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device_.handle(), view, nullptr);
    }
    views_.clear();
    for (auto& signal : release_) {
        if (signal != VK_NULL_HANDLE) vkDestroySemaphore(device_.handle(), signal, nullptr);
    }
    release_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_.handle(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();
}

bool Window::key_held(int key) const {
    if (headless_) {
        const auto found = held_.find(key);
        return found != held_.end() && found->second;
    }
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

bool Window::button_held(int button) const {
    if (headless_) return button >= 0 && button < 8 && held_buttons_[button];
    return glfwGetMouseButton(window_, button) == GLFW_PRESS;
}

bool Window::open() const {
    if (headless_) return !closed_;
    return window_ != nullptr && glfwWindowShouldClose(window_) == GLFW_FALSE;
}

// `KeyMapping.isDown`: a binding is held when the key or the mouse button it
// is on is held, and a binding on nothing is never held.
bool Window::bound_down(std::string_view binding, int fallback_key, bool fallback_is_mouse) const {
    const Keybind* bind = keybinds_ != nullptr ? keybinds_->find(binding) : nullptr;
    if (bind == nullptr) {
        return fallback_is_mouse ? button_held(fallback_key) : key_held(fallback_key);
    }
    if (bind->current.unbound()) return false;
    return bind->current.type == InputKey::Type::Mouse ? button_held(bind->current.code)
                                                       : key_held(bind->current.code);
}

FrameInput Window::poll() {
    if (!headless_) glfwPollEvents();
    FrameInput input;

    const auto down = [&](int key) { return key_held(key); };
    const auto bound = [&](std::string_view binding, int fallback) {
        return bound_down(binding, fallback, false);
    };
    const auto bound_mouse = [&](std::string_view binding, int fallback) {
        return bound_down(binding, fallback, true);
    };

    const bool enter = down(GLFW_KEY_ENTER);
    const bool escape = down(GLFW_KEY_ESCAPE);
    const bool backspace = down(GLFW_KEY_BACKSPACE);
    const bool enter_edge = enter && !enter_down_;
    const bool escape_edge = escape && !escape_down_;
    const bool backspace_edge = backspace && !backspace_down_;
    enter_down_ = enter;
    escape_down_ = escape;
    backspace_down_ = backspace;

    if (typing_) {
        input.submit_text = enter_edge;
        input.cancel_text = escape_edge;
        if (backspace_edge && !typed_.empty()) typed_.pop_back();
        return input;
    }

    const bool chat_now = bound("key.chat", GLFW_KEY_T);
    input.start_typing = chat_now && !chat_key_down_;
    chat_key_down_ = chat_now;

    const bool forward_now = bound("key.forward", GLFW_KEY_W);
    if (forward_now) input.forward += 1.0f;
    if (bound("key.back", GLFW_KEY_S)) input.forward -= 1.0f;
    if (bound("key.right", GLFW_KEY_D)) input.right += 1.0f;
    if (bound("key.left", GLFW_KEY_A)) input.right -= 1.0f;
    const bool jump_now = bound("key.jump", GLFW_KEY_SPACE);
    if (jump_now) input.up += 1.0f;
    input.jump = jump_now;
    input.sneak = bound("key.sneak", GLFW_KEY_LEFT_SHIFT);
    if (input.sneak) input.up -= 1.0f;

    // Forwards twice in quick succession sprints until forwards comes up, which
    // is how most people sprint; the key is there for the rest.
    const double seconds_now = headless_ ? clock_ : glfwGetTime();
    if (forward_now && !forward_key_down_) {
        double_tap_sprint_ = seconds_now - last_forward_press_ < 0.3;
        last_forward_press_ = seconds_now;
    }
    if (!forward_now) double_tap_sprint_ = false;
    forward_key_down_ = forward_now;
    input.sprint_key = bound("key.sprint", GLFW_KEY_LEFT_CONTROL);
    input.sprint = input.sprint_key || double_tap_sprint_;
    // Escape opens and closes the menu, the way it does in the game; leaving
    // is what the window's own close button is for.
    input.toggle_menu = escape_edge;

    // Jump twice quickly to fly, the way the game does it in creative.
    constexpr double kDoubleTapSeconds = 0.3;
    if (jump_now && !jump_key_down_) {
        input.toggle_fly = seconds_now - last_jump_press_ < kDoubleTapSeconds;
        last_jump_press_ = input.toggle_fly ? -1.0 : seconds_now;
    }
    jump_key_down_ = jump_now;

    const bool drop_now = bound("key.drop", GLFW_KEY_Q);
    input.drop = drop_now && !drop_key_down_;
    // `Minecraft.handleKeybinds`: control and drop throws the whole stack.
    input.control = down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL);
    input.drop_stack = input.drop && input.control;
    drop_key_down_ = drop_now;

    const bool swap_now = bound("key.swapOffhand", GLFW_KEY_F);
    input.swap_offhand = swap_now && !swap_key_down_;
    swap_key_down_ = swap_now;

    // The wheel steps one slot at a time whatever the mouse reports, and the
    // number row picks a slot outright, the way the game does both.
    input.wheel = wheel_ > 0.5 ? 1 : (wheel_ < -0.5 ? -1 : 0);
    wheel_ = 0.0;
    for (int i = 0; i < 9; ++i) {
        char name[] = "key.hotbar.0";
        name[11] = static_cast<char>('1' + i);
        const bool pressed = bound(name, GLFW_KEY_1 + i);
        if (pressed && !slot_key_down_[i]) input.pick_slot = i;
        slot_key_down_[i] = pressed;
    }

    input.attack = bound_mouse("key.attack", GLFW_MOUSE_BUTTON_LEFT);
    input.use = bound_mouse("key.use", GLFW_MOUSE_BUTTON_RIGHT);

    const bool inventory_now = bound("key.inventory", GLFW_KEY_E);
    input.toggle_inventory = inventory_now && !inventory_key_down_;
    inventory_key_down_ = inventory_now;

    const bool menu_now = down(GLFW_KEY_M);
    input.toggle_menu = input.toggle_menu || (menu_now && !menu_key_down_);
    menu_key_down_ = menu_now;

    double x = pending_x_, y = pending_y_;
    if (!headless_) glfwGetCursorPos(window_, &x, &y);
    if (have_cursor_ && cursor_captured_) {
        input.yaw_delta = static_cast<float>(x - last_x_);
        input.pitch_delta = static_cast<float>(y - last_y_);
    }
    last_x_ = x;
    last_y_ = y;
    have_cursor_ = true;
    input.cursor_x = static_cast<float>(x);
    input.cursor_y = static_cast<float>(y);

    // `click` and the two click counters are the left and right buttons
    // themselves: the feature menu's pointer and the cps readout follow the
    // mouse, not whatever key attack happens to be on.
    const bool pressed = button_held(GLFW_MOUSE_BUTTON_LEFT);
    input.click = pressed && !mouse_down_;
    mouse_down_ = pressed;
    const bool right = button_held(GLFW_MOUSE_BUTTON_RIGHT);

    // The three the game counts as presses rather than as a held button.
    input.attack_clicks = input.attack && !attack_key_down_ ? 1 : 0;
    attack_key_down_ = input.attack;
    input.use_clicks = input.use && !use_key_down_ ? 1 : 0;
    use_key_down_ = input.use;
    const bool pick_now = bound_mouse("key.pickItem", GLFW_MOUSE_BUTTON_MIDDLE);
    input.pick_item_clicks = pick_now && !pick_key_down_ ? 1 : 0;
    pick_key_down_ = pick_now;

    const auto now = std::chrono::steady_clock::now();
    if (input.click) left_clicks_.push_back(now);
    if (right && !right_down_) right_clicks_.push_back(now);
    right_down_ = right;
    const auto second_ago = now - std::chrono::seconds(1);
    while (!left_clicks_.empty() && left_clicks_.front() < second_ago) left_clicks_.pop_front();
    while (!right_clicks_.empty() && right_clicks_.front() < second_ago) right_clicks_.pop_front();
    return input;
}

void Window::set_vsync(bool on) {
    if (on == vsync_) return;
    vsync_ = on;
    swapchain_stale_ = true;
}

void Window::begin_typing() {
    typing_ = true;
    typed_.clear();
    capture_cursor(false);
}

void Window::end_typing() {
    typing_ = false;
    typed_.clear();
    capture_cursor(true);
}

std::vector<InputEvent> Window::take_events() {
    std::vector<InputEvent> out;
    out.swap(events_);
    return out;
}

std::string Window::clipboard() const {
    if (headless_) return clipboard_;
    const char* text = glfwGetClipboardString(window_);
    return text != nullptr ? std::string(text) : std::string{};
}

void Window::set_clipboard(const std::string& text) {
    if (headless_) {
        clipboard_ = text;
        return;
    }
    glfwSetClipboardString(window_, text.c_str());
}

double Window::content_scale() const {
    if (headless_) return 1.0;
    int window_w = 0, window_h = 0, frame_w = 0, frame_h = 0;
    glfwGetWindowSize(window_, &window_w, &window_h);
    glfwGetFramebufferSize(window_, &frame_w, &frame_h);
    return window_w > 0 ? static_cast<double>(frame_w) / window_w : 1.0;
}

int Window::clicks_per_second(bool right_button) const {
    return static_cast<int>(right_button ? right_clicks_.size() : left_clicks_.size());
}

void Window::capture_cursor(bool captured) {
    if (captured == cursor_captured_) return;
    cursor_captured_ = captured;
    if (!headless_) {
        glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    // The jump from wherever the pointer was to wherever it is now is not a
    // look, so the next frame starts a fresh delta.
    have_cursor_ = false;
}

void Window::present(WorldRenderer& renderer, const Camera& camera,
                     const Overlay& overlay) {
    const auto started = std::chrono::steady_clock::now();
    if (headless_) {
        // The same frame the window would show, read back when asked for.
        if (capture_) {
            pixels_ = renderer.render(camera, overlay);
        } else {
            renderer.draw(camera, overlay);
        }
        last_frame_ms_ = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started).count();
        return;
    }
    if (swapchain_stale_) {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        if (w == 0 || h == 0) return;
        swapchain_stale_ = false;
        vkDeviceWaitIdle(device_.handle());
        destroy_swapchain();
        create_swapchain();
    }
    Frame& frame = frames_[frame_index_];

    check(vkWaitForFences(device_.handle(), 1, &frame.in_flight, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");
    // That fence belonged to a frame two back, so its timestamps are ready.
    // Reading them here is the only honest moment: right after submitting, the
    // gpu has not run yet and the query still holds the previous value. The
    // guard matters: the fences start signalled, so without it the first frame
    // waits on a query nothing has written.
    if (presented_once_) renderer.read_timestamps();

    uint32_t index = 0;
    VkResult acquired = vkAcquireNextImageKHR(device_.handle(), swapchain_, UINT64_MAX,
                                              frame.acquired, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(device_.handle());
        destroy_swapchain();
        create_swapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }

    check(vkResetFences(device_.handle(), 1, &frame.in_flight), "vkResetFences");
    check(vkResetCommandBuffer(frame.command, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");

    renderer.record_present(frame.command, camera);

    transition(frame.command, images_[index], VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit2 blit{};
    blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1].x = static_cast<int32_t>(renderer.width());
    blit.srcOffsets[1].y = static_cast<int32_t>(renderer.height());
    blit.srcOffsets[1].z = 1;
    blit.dstOffsets[1].x = static_cast<int32_t>(extent_.width);
    blit.dstOffsets[1].y = static_cast<int32_t>(extent_.height);
    blit.dstOffsets[1].z = 1;

    VkBlitImageInfo2 blit_info{};
    blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blit_info.srcImage = renderer.color_image();
    blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit_info.dstImage = images_[index];
    blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit_info.regionCount = 1;
    blit_info.pRegions = &blit;
    blit_info.filter = VK_FILTER_NEAREST;
    vkCmdBlitImage2(frame.command, &blit_info);

    if (overlay) {
        transition(frame.command, images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo colour{};
        colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colour.imageView = views_[index];
        colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = {{0, 0}, extent_};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &colour;

        vkCmdBeginRendering(frame.command, &rendering);
        overlay(frame.command, extent_.width, extent_.height);
        vkCmdEndRendering(frame.command);

        transition(frame.command, images_[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    } else {
        transition(frame.command, images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    }

    check(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = frame.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;

    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = release_[index];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo command_submit{};
    command_submit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_submit.commandBuffer = frame.command;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_submit;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    check(vkQueueSubmit2(device_.graphics_queue(), 1, &submit, frame.in_flight), "vkQueueSubmit2");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &release_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const VkResult presented = vkQueuePresentKHR(device_.graphics_queue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(device_.handle());
        destroy_swapchain();
        create_swapchain();
    } else {
        check(presented, "vkQueuePresentKHR");
    }

    presented_once_ = true;
    frame_index_ = (frame_index_ + 1) % frames_.size();
    last_frame_ms_ = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - started)
                         .count();
}

} // namespace mcpp::gpu

namespace mcpp::gpu {

bool Window::key_down(int glfw_key) const {
    if (headless_) {
        const auto found = held_.find(glfw_key);
        return found != held_.end() && found->second;
    }
    return glfw_key >= 0 && glfwGetKey(window_, glfw_key) == GLFW_PRESS;
}

} // namespace mcpp::gpu
