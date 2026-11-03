#pragma once
#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/text.hpp"
#include "mcpp/gpu/world_renderer.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace mcpp::gpu {

// What the player did since the last frame, in the units the camera wants.
struct FrameInput {
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float yaw_delta = 0.0f;
    float pitch_delta = 0.0f;
    bool sprint = false;
    bool quit = false;
    // Where the pointer is, and whether it was pressed this frame. Only
    // meaningful while the cursor is free, which is while a menu is up.
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    bool click = false;
    // Edge-triggered: true on the frame the key goes down, not while held.
    bool toggle_menu = false;
};

// GLFW only for the surface and the key state; everything after the surface is
// ours. The swapchain owns no pipeline: a frame is drawn by WorldRenderer into
// its own image and blitted here, so the offscreen and window paths cannot
// drift apart.
class Window {
public:
    // Call before Device::create; returns the instance extensions the surface
    // needs, which have to be chosen before the instance exists.
    static std::vector<const char*> required_instance_extensions();

    Window(Device& device, uint32_t width, uint32_t height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool open() const;
    FrameInput poll();
    // Captured means the pointer is hidden and locked to the window, which is
    // what looking around needs and what a menu must undo.
    void capture_cursor(bool captured);
    bool cursor_captured() const { return cursor_captured_; }
    // `overlay`, when given, is recorded over the swapchain image after the
    // world, at screen resolution rather than scaled up from the render target.
    using Overlay = std::function<void(VkCommandBuffer, uint32_t, uint32_t)>;
    void present(WorldRenderer& renderer, const Camera& camera, const Overlay& overlay = {});
    VkFormat colour_format() const { return format_; }

    uint32_t width() const { return extent_.width; }
    uint32_t height() const { return extent_.height; }
    double last_frame_ms() const { return last_frame_ms_; }

private:
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };

    void create_swapchain();
    void destroy_swapchain();

    Device& device_;
    GLFWwindow* window_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> release_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<Frame> frames_;
    size_t frame_index_ = 0;
    bool presented_once_ = false;

    double last_x_ = 0.0;
    double last_y_ = 0.0;
    bool have_cursor_ = false;
    bool menu_key_down_ = false;
    bool mouse_down_ = false;
    bool cursor_captured_ = false;
    double last_frame_ms_ = 0.0;
};

} // namespace mcpp::gpu
