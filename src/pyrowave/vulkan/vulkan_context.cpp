// SPDX-License-Identifier: MIT
#include "vulkan_context.hpp"
#ifdef __PROSPERO__
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance,
                                                                              const char *);
#endif
static void require(bool ok, const char *reason)
{
    if (!ok)
    {
        log_line("PYROWAVE_UNSUPPORTED: %s", reason);
        fail(reason);
    }
}
void VulkanContext::init(bool presentation)
{
    Util::register_thread_index(0);
#ifdef __PROSPERO__
    volkInitializeCustom(vk_icdGetInstanceProcAddr);
#else
    VK_OK(volkInitialize());
#endif
    require(vkCreateInstance != nullptr, "no Vulkan instance entry point");
    app.pApplicationName = "ProsperoLight PyroWave";
    app.apiVersion = VK_API_VERSION_1_3;
    ici.pApplicationInfo = &app;
    static const char *instance_ext[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                         VK_KHR_DISPLAY_EXTENSION_NAME,
                                         VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME};
    if (presentation)
    {
        ici.enabledExtensionCount = 3;
        ici.ppEnabledExtensionNames = instance_ext;
    }
    VK_OK(vkCreateInstance(&ici, nullptr, &instance));
    volkLoadInstance(instance);
    uint32_t n = 0;
    VK_OK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    require(n > 0, "no physical device");
    std::vector<VkPhysicalDevice> devices(n);
    VK_OK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    physical = devices[0];
    driver.pNext = &subgroup;
    subgroup.pNext = &control;
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical, &p2);
    props = p2.properties;
    log_line("device=%s driver=%s info=%s Vulkan=%u.%u.%u", props.deviceName, driver.driverName,
             driver.driverInfo, VK_API_VERSION_MAJOR(props.apiVersion),
             VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    log_line("subgroupSize=%u stages=0x%x operations=0x%x min=%u max=%u requiredStages=0x%x "
             "maxComputeWorkgroupSubgroups=%u",
             subgroup.subgroupSize, subgroup.supportedStages, subgroup.supportedOperations,
             control.minSubgroupSize, control.maxSubgroupSize, control.requiredSubgroupSizeStages,
             control.maxComputeWorkgroupSubgroups);
    log_line("timestampPeriod=%.9g ns timestampComputeAndGraphics=%u "
             "maxComputeWorkGroupSize=%u,%u,%u invocations=%u maxTexelBufferElements=%u",
             props.limits.timestampPeriod, props.limits.timestampComputeAndGraphics,
             props.limits.maxComputeWorkGroupSize[0], props.limits.maxComputeWorkGroupSize[1],
             props.limits.maxComputeWorkGroupSize[2], props.limits.maxComputeWorkGroupInvocations,
             props.limits.maxTexelBufferElements);
    features.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    log_line("subgroupSizeControl=%u computeFullSubgroups=%u synchronization2=%u "
             "timelineSemaphore=%u storageBuffer8BitAccess=%u shaderInt16=%u",
             f13.subgroupSizeControl, f13.computeFullSubgroups, f13.synchronization2,
             f12.timelineSemaphore, f12.storageBuffer8BitAccess, features.features.shaderInt16);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.data());
    for (uint32_t i = 0; i < n; i++)
    {
        log_line("queue[%u] flags=0x%x count=%u timestampValidBits=%u", i, families[i].queueFlags,
                 families[i].queueCount, families[i].timestampValidBits);
        if (family == UINT32_MAX && families[i].queueCount &&
            (families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            family = i;
            timestamp_bits = families[i].timestampValidBits;
        }
    }
    const VkSubgroupFeatureFlags ops[] = {
        VK_SUBGROUP_FEATURE_BASIC_BIT,      VK_SUBGROUP_FEATURE_VOTE_BIT,
        VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, VK_SUBGROUP_FEATURE_BALLOT_BIT,
        VK_SUBGROUP_FEATURE_SHUFFLE_BIT,    VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT};
    const char *names[] = {"BASIC", "VOTE", "ARITHMETIC", "BALLOT", "SHUFFLE", "SHUFFLE_RELATIVE"};
    for (unsigned i = 0; i < 6; i++)
        log_line("compute subgroup %s=%s", names[i],
                 (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                         (subgroup.supportedOperations & ops[i])
                     ? "YES"
                     : "NO");
    require(props.apiVersion >= VK_API_VERSION_1_3, "Vulkan < 1.3");
#ifdef __PROSPERO__
    require(driver.driverID == VK_DRIVER_ID_MESA_RADV, "device is not Mesa RADV");
#endif
    require(family != UINT32_MAX, "no graphics+compute queue");
    require(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT,
            "compute subgroup stage missing");
    for (unsigned i = 0; i < 6; i++)
        require(subgroup.supportedOperations & ops[i], names[i]);
    require(f13.subgroupSizeControl, "subgroupSizeControl missing");
    require(f13.computeFullSubgroups,
            "computeFullSubgroups missing (pinned decoder requires full groups)");
    require(control.minSubgroupSize <= 128 && control.maxSubgroupSize >= 4,
            "no subgroup size overlap with wave4..128");
    require((control.minSubgroupSize >= 4 && control.maxSubgroupSize <= 128) ||
                (control.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT),
            "cannot select compatible compute subgroup size");
    require(props.limits.maxComputeWorkGroupSize[0] >= 128 &&
                props.limits.maxComputeWorkGroupInvocations >= 128,
            "decoder needs 128-thread workgroups");
    require(f13.synchronization2, "Granite synchronization2 missing");
    require(timestamp_bits > 0 && timestamp_bits <= 64 && props.limits.timestampPeriod > 0 &&
                props.limits.timestampComputeAndGraphics,
            "graphics+compute timestamps unavailable");
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16_UNORM, &format);
    require((format.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) ==
                (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT),
            "R16_UNORM storage/transfer-src unavailable");
    require(features.features.shaderStorageImageExtendedFormats,
            "R8 storage image extended formats missing");
    // Enable supported core features Granite may use; encoder-only features are never
    // prerequisites.
    features.features.robustBufferAccess = VK_FALSE;
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    static const char *device_ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (presentation)
    {
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = device_ext;
    }
    VK_OK(vkCreateDevice(physical, &dci, nullptr, &device));
    volkLoadDevice(device);
    vkGetDeviceQueue(device, family, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    log_line("RADV Vulkan device created; selected queue=%u", family);
    qi = {queue, family, 0};
#ifdef __PROSPERO__
    pci.GetInstanceProcAddr = vk_icdGetInstanceProcAddr;
#else
    pci.GetInstanceProcAddr = vkGetInstanceProcAddr;
#endif
    pci.instance = instance;
    pci.physical_device = physical;
    pci.device = device;
    pci.instance_create_info = &ici;
    pci.device_create_info = &dci;
    pci.queue_info = &qi;
    pci.queue_info_count = 1;
    PW_OK(pyrowave_create_device(&pci, &pyro));
    PW_OK(pyrowave_device_set_queue_type(pyro, VK_QUEUE_GRAPHICS_BIT));
    log_line("PyroWave device created commit=%s", PYRO_COMMIT);
    VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_OK(vkCreateCommandPool(device, &pi, nullptr, &pool));
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.commandBufferCount = 1;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VK_OK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_OK(vkCreateFence(device, &fi, nullptr, &fence));
    VkQueryPoolCreateInfo qi2 = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi2.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi2.queryCount = 4;
    VK_OK(vkCreateQueryPool(device, &qi2, nullptr, &queries));
}
void VulkanContext::begin()
{
    VK_OK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_OK(vkBeginCommandBuffer(cmd, &bi));
}
void VulkanContext::submit_wait()
{
    VK_OK(vkEndCommandBuffer(cmd));
    VK_OK(vkResetFences(device, 1, &fence));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_OK(vkQueueSubmit(queue, 1, &si, fence));
    VK_OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 30000000000ull));
}
unsigned VulkanContext::memory_type(uint32_t bits, VkMemoryPropertyFlags required)
{
    for (unsigned i = 0; i < memory.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & required) == required)
            return i;
    fail("no compatible Vulkan memory type");
}
VulkanContext::~VulkanContext()
{
    if (device)
        vkDeviceWaitIdle(device);
    if (pyro)
        pyrowave_device_destroy(pyro);
    if (device)
    {
        if (queries)
            vkDestroyQueryPool(device, queries, nullptr);
        if (fence)
            vkDestroyFence(device, fence, nullptr);
        if (pool)
            vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance)
        vkDestroyInstance(instance, nullptr);
}
Output::Output(VulkanContext &ctx, unsigned w, unsigned h, unsigned depth, bool chroma444)
    : c(ctx), bit_depth(depth)
{
    for (unsigned p = 0; p < 3; p++)
    {
        auto &plane = planes[p];
        plane.width = p && !chroma444 ? w / 2 : w;
        plane.height = p && !chroma444 ? h / 2 : h;
        VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = depth == 10 ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
        ii.extent = {plane.width, plane.height, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VK_OK(vkCreateImage(c.device, &ii, nullptr, &plane.image));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(c.device, plane.image, &req);
        VkMemoryAllocateInfo mi = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mi.allocationSize = req.size;
        mi.memoryTypeIndex = c.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_OK(vkAllocateMemory(c.device, &mi, nullptr, &plane.allocation));
        VK_OK(vkBindImageMemory(c.device, plane.image, plane.allocation, 0));
        auto &v = views.planes[p];
        v.image = plane.image;
        v.width = plane.width;
        v.height = plane.height;
        v.image_format = v.view_format = ii.format;
        v.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        v.swizzle = VK_COMPONENT_SWIZZLE_IDENTITY;
        v.layout = VK_IMAGE_LAYOUT_GENERAL;
    }
}
void Output::prepare()
{
    prepare(c.cmd);
}
void Output::prepare(VkCommandBuffer cmd)
{
    VkImageMemoryBarrier b[3] = {};
    for (unsigned p = 0; p < 3; p++)
    {
        b[p].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[p].image = planes[p].image;
        b[p].srcQueueFamilyIndex = b[p].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[p].oldLayout = initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        b[p].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b[p].srcAccessMask =
            initialized ? VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT : 0;
        b[p].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b[p].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    vkCmdPipelineBarrier(
        cmd, initialized ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 3, b);
    initialized = true;
}
Output::~Output()
{
    for (auto &p : planes)
    {
        if (p.image)
            vkDestroyImage(c.device, p.image, nullptr);
        if (p.allocation)
            vkFreeMemory(c.device, p.allocation, nullptr);
    }
}
