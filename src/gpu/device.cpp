#include "mcpp/gpu/device.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

namespace mcpp::gpu {
namespace {

constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

void check(VkResult result, std::string_view what) {
    if (result != VK_SUCCESS) {
        throw ProtocolError(std::format("{} faalde met VkResult {}", what,
                                        static_cast<int>(result)));
    }
}

bool has_extension(const std::vector<VkExtensionProperties>& available, const char* wanted) {
    for (const auto& extension : available) {
        if (std::strcmp(extension.extensionName, wanted) == 0) return true;
    }
    return false;
}

std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice device) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return extensions;
}

AdapterInfo describe(VkPhysicalDevice device) {
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(device, &properties);

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13.pNext = &vulkan12;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vulkan13;
    vkGetPhysicalDeviceFeatures2(device, &features);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(device, &memory);

    AdapterInfo info;
    info.name = properties.properties.deviceName;
    info.type = properties.properties.deviceType;
    info.driver = std::format("{} {}", driver.driverName, driver.driverInfo);
    info.api_version = properties.properties.apiVersion;

    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            info.device_local_bytes += memory.memoryHeaps[i].size;
        }
    }

    const auto extensions = device_extensions(device);
    info.capabilities.mesh_shader = has_extension(extensions, "VK_EXT_mesh_shader");
    info.capabilities.descriptor_indexing = vulkan12.descriptorIndexing == VK_TRUE;
    info.capabilities.buffer_device_address = vulkan12.bufferDeviceAddress == VK_TRUE;
    info.capabilities.timeline_semaphore = vulkan12.timelineSemaphore == VK_TRUE;
    info.capabilities.host_query_reset = vulkan12.hostQueryReset == VK_TRUE;
    info.capabilities.dynamic_rendering = vulkan13.dynamicRendering == VK_TRUE;
    info.capabilities.synchronization2 = vulkan13.synchronization2 == VK_TRUE;
    info.capabilities.demote_to_helper =
        vulkan13.shaderDemoteToHelperInvocation == VK_TRUE;
    return info;
}

VkInstance create_instance(bool validation) {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Plodder Client";
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "Plodder";
    application.apiVersion = kApiVersion;

    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    if (validation) {
        create.enabledLayerCount = 1;
        create.ppEnabledLayerNames = &kValidationLayer;
    }

    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
    return instance;
}

int score(const AdapterInfo& info, bool prefer_discrete) {
    int points = 0;
    if (prefer_discrete && info.type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) points += 1000;
    if (info.capabilities.mesh_shader) points += 100;
    if (info.capabilities.descriptor_indexing) points += 50;
    if (info.capabilities.buffer_device_address) points += 50;
    points += static_cast<int>(info.device_local_bytes / (256ull * 1024 * 1024));
    return points;
}

} // namespace

std::string_view adapter_type_name(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "losse gpu";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "geintegreerde gpu";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtuele gpu";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
    default:                                     return "onbekend";
    }
}

std::string api_version_string(uint32_t version) {
    return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                       VK_API_VERSION_PATCH(version));
}

bool Device::validation_available() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) return true;
    }
    return false;
}

std::filesystem::path Device::default_pipeline_cache() {
    std::filesystem::path base;
    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
        base = cache_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".cache";
    }
    return base / "plodder-client" / "pipelines.bin";
}

std::vector<AdapterInfo> Device::list_adapters() {
    VkInstance instance = create_instance(false);

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    std::vector<AdapterInfo> adapters;
    adapters.reserve(devices.size());
    for (VkPhysicalDevice device : devices) adapters.push_back(describe(device));

    vkDestroyInstance(instance, nullptr);
    return adapters;
}

void Device::create(const Options& options) {
    destroy();
    cache_path_ = options.pipeline_cache;

    instance_ = create_instance(options.validation && validation_available());

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw ProtocolError("geen enkele gpu met Vulkan-ondersteuning gevonden");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    int best = -1;
    for (VkPhysicalDevice candidate : devices) {
        auto info = describe(candidate);
        if (info.api_version < kApiVersion) continue;
        const int points = score(info, options.prefer_discrete);
        if (points > best) {
            best = points;
            physical_ = candidate;
            adapter_ = std::move(info);
        }
    }
    if (physical_ == VK_NULL_HANDLE) {
        throw ProtocolError(std::format("geen gpu met Vulkan {}", api_version_string(kApiVersion)));
    }

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &family_count, families.data());

    bool found = false;
    for (uint32_t i = 0; i < family_count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family_ = i;
            found = true;
            break;
        }
    }
    if (!found) throw ProtocolError("gpu heeft geen grafische wachtrij");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = graphics_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan12Features vulkan12{};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12.descriptorIndexing = adapter_.capabilities.descriptor_indexing;
    vulkan12.bufferDeviceAddress = adapter_.capabilities.buffer_device_address;
    vulkan12.timelineSemaphore = adapter_.capabilities.timeline_semaphore;
    vulkan12.hostQueryReset = adapter_.capabilities.host_query_reset;

    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13.dynamicRendering = adapter_.capabilities.dynamic_rendering;
    vulkan13.synchronization2 = adapter_.capabilities.synchronization2;
    vulkan13.shaderDemoteToHelperInvocation = adapter_.capabilities.demote_to_helper;
    vulkan13.pNext = &vulkan12;

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &vulkan13;

    VkDeviceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create.pNext = &features;
    create.queueCreateInfoCount = 1;
    create.pQueueCreateInfos = &queue;

    check(vkCreateDevice(physical_, &create, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);

    // A warm cache on disk is what keeps startup off the driver's compiler.
    std::vector<uint8_t> blob;
    if (std::ifstream file(cache_path_, std::ios::binary); file) {
        blob.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        cache_bytes_loaded_ = blob.size();
    }
    VkPipelineCacheCreateInfo cache{};
    cache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cache.initialDataSize = blob.size();
    cache.pInitialData = blob.empty() ? nullptr : blob.data();
    if (vkCreatePipelineCache(device_, &cache, nullptr, &pipeline_cache_) != VK_SUCCESS) {
        cache.initialDataSize = 0;
        cache.pInitialData = nullptr;
        cache_bytes_loaded_ = 0;
        check(vkCreatePipelineCache(device_, &cache, nullptr, &pipeline_cache_),
              "vkCreatePipelineCache");
    }
}

void Device::save_pipeline_cache() const {
    if (pipeline_cache_ == VK_NULL_HANDLE || cache_path_.empty()) return;

    size_t bytes = 0;
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &bytes, nullptr) != VK_SUCCESS) return;
    std::vector<uint8_t> blob(bytes);
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &bytes, blob.data()) != VK_SUCCESS) return;

    std::error_code error;
    std::filesystem::create_directories(cache_path_.parent_path(), error);
    std::ofstream file(cache_path_, std::ios::binary);
    file.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(bytes));
}

void Device::destroy() {
    if (pipeline_cache_ != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
        pipeline_cache_ = VK_NULL_HANDLE;
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physical_ = VK_NULL_HANDLE;
    graphics_queue_ = VK_NULL_HANDLE;
}

Device::~Device() { destroy(); }

} // namespace mcpp::gpu
