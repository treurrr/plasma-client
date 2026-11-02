#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace mcpp::gpu {

struct DeviceCapabilities {
    bool mesh_shader = false;
    bool descriptor_indexing = false;
    bool buffer_device_address = false;
    bool dynamic_rendering = false;
    bool synchronization2 = false;
    bool timeline_semaphore = false;
    bool host_query_reset = false;
    bool demote_to_helper = false;
};

struct AdapterInfo {
    std::string name;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    std::string driver;
    uint32_t api_version = 0;
    uint64_t device_local_bytes = 0;
    DeviceCapabilities capabilities;
};

std::string_view adapter_type_name(VkPhysicalDeviceType type);
std::string api_version_string(uint32_t version);

// Raw Vulkan on purpose: the startup and frame-pacing claims rest on owning the
// pipeline cache, the submission path and the memory layout.
class Device {
public:
    struct Options {
        bool validation = false;
        bool prefer_discrete = true;
        std::filesystem::path pipeline_cache = default_pipeline_cache();
    };

    Device() = default;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    void create(const Options& options);
    void destroy();

    // Usable before create(): enumerates without keeping a device alive.
    static std::vector<AdapterInfo> list_adapters();
    static bool validation_available();
    static std::filesystem::path default_pipeline_cache();

    const AdapterInfo& adapter() const { return adapter_; }
    VkDevice handle() const { return device_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    uint32_t graphics_family() const { return graphics_family_; }
    VkPipelineCache pipeline_cache() const { return pipeline_cache_; }
    size_t pipeline_cache_bytes_loaded() const { return cache_bytes_loaded_; }

    void save_pipeline_cache() const;

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    uint32_t graphics_family_ = 0;
    size_t cache_bytes_loaded_ = 0;
    AdapterInfo adapter_;
    std::filesystem::path cache_path_;
};

} // namespace mcpp::gpu
