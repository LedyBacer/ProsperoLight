// SPDX-License-Identifier: MIT
#pragma once
#include <volk.h>
#include <pyrowave.h>
#include "../common.hpp"
#define VK_OK(expr)                                                                                \
    do                                                                                             \
    {                                                                                              \
        VkResult r_ = (expr);                                                                      \
        if (r_ != VK_SUCCESS)                                                                      \
        {                                                                                          \
            log_line("%s: VkResult=%d", #expr, int(r_));                                           \
            fail(#expr);                                                                           \
        }                                                                                          \
    } while (0)
#define PW_OK(expr)                                                                                \
    do                                                                                             \
    {                                                                                              \
        pyrowave_result r_ = (expr);                                                               \
        if (r_ != PYROWAVE_SUCCESS)                                                                \
        {                                                                                          \
            log_line("%s: pyrowave_result=%d", #expr, int(r_));                                    \
            fail(#expr);                                                                           \
        }                                                                                          \
    } while (0)

struct VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = UINT32_MAX, timestamp_bits = 0;
    VkPhysicalDeviceMemoryProperties memory = {};
    VkPhysicalDeviceProperties props = {};
    VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceSubgroupProperties subgroup = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceSubgroupSizeControlProperties control = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    // All borrowed create-info pointers have context lifetime. Context is never copied.
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features f11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    pyrowave_device_create_queue_info qi = {};
    pyrowave_device_create_info pci = {};
    pyrowave_device pyro = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    VulkanContext() = default;
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;
    void init(bool presentation = false);
    void begin();
    void submit_wait();
    unsigned memory_type(uint32_t bits, VkMemoryPropertyFlags required);
    ~VulkanContext();
};
struct Plane
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory allocation = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
};
struct Output
{
    VulkanContext &c;
    Plane planes[3];
    bool initialized = false;
    pyrowave_gpu_buffers views = {};
    unsigned bit_depth = 8;
    explicit Output(VulkanContext &ctx, unsigned w, unsigned h, unsigned depth = 8,
                    bool chroma444 = false);
    void prepare(VkCommandBuffer cmd);
    void prepare();
    ~Output();
};

// Granite exposes one thread index for the externally serialized decoder.
namespace Util
{
void register_thread_index(unsigned index);
}
