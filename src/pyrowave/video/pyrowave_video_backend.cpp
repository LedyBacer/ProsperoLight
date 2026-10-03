#include "presentation_preferences.hpp"
// SPDX-License-Identifier: MIT
#include "pyrowave_video_backend.hpp"
#include <ctime>
extern "C" int wsi_ps5_configure_output(int hdr, int vsync, int high_refresh);
static double clock_ms()
{
    timespec t = {};
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        fail("clock");
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
void PyroWaveVideoBackend::initialize(unsigned width, unsigned height, unsigned fps, bool chroma444,
                                      bool hdr, bool vsync, bool tv_safe)
{
    const bool request_vrr = moonlight::presentation_mode() == 2;
    const bool prefer_high_refresh = fps > 60 || request_vrr;
    const int output_flags = (fps > 60 || request_vrr ? 1 : 0) | (request_vrr ? 2 : 0);
    vsync = vsync || request_vrr;
    refresh_ = 0;
    requested_ = 0;
    if (wsi_ps5_configure_output(hdr, vsync, output_flags) != 0)
        fail("VideoOut still owned by previous session");
    if (!decoder_.initialize(c_, width, height, chroma444))
        fail("decoder initialize");
    output_ = std::make_unique<Output>(c_, width, height, hdr ? 10 : 8, chroma444);
    uint32_t n = 0;
    VK_OK(vkGetPhysicalDeviceDisplayPropertiesKHR(c_.physical, &n, nullptr));
    if (!n)
        fail("no display");
    std::vector<VkDisplayPropertiesKHR> displays(n);
    VK_OK(vkGetPhysicalDeviceDisplayPropertiesKHR(c_.physical, &n, displays.data()));
    VK_OK(vkGetDisplayModePropertiesKHR(c_.physical, displays[0].display, &n, nullptr));
    std::vector<VkDisplayModePropertiesKHR> modes(n);
    VK_OK(vkGetDisplayModePropertiesKHR(c_.physical, displays[0].display, &n, modes.data()));
    VkDisplayModeKHR selected = VK_NULL_HANDLE;
    for (auto &m : modes)
    {
        log_line("display mode %ux%u %.3f Hz", m.parameters.visibleRegion.width,
                 m.parameters.visibleRegion.height, m.parameters.refreshRate / 1000.0);
        if ((prefer_high_refresh || m.parameters.refreshRate < 70000) &&
            m.parameters.visibleRegion.width == 3840 && m.parameters.visibleRegion.height == 2160 &&
            m.parameters.refreshRate / 1000.0 > refresh_)
        {
            selected = m.displayMode;
            refresh_ = m.parameters.refreshRate / 1000.0;
        }
    }
    if (!selected)
        fail("no 4K mode");
    if (prefer_high_refresh && refresh_ < 100)
        log_line("120Hz unavailable: explicit 60Hz fallback; 4K120 criterion pending");
    VkDisplaySurfaceCreateInfoKHR si = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    si.displayMode = selected;
    si.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    si.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    si.imageExtent = {3840, 2160};
    VK_OK(vkCreateDisplayPlaneSurfaceKHR(c_.instance, &si, nullptr, &surface_));
    VkBool32 support = VK_FALSE;
    VK_OK(vkGetPhysicalDeviceSurfaceSupportKHR(c_.physical, c_.family, surface_, &support));
    if (!support)
        fail("queue cannot present");
    VkSurfaceCapabilitiesKHR caps;
    VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c_.physical, surface_, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        fail("swapchain color attachment unsupported");
    VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(c_.physical, surface_, &n, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(n);
    VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(c_.physical, surface_, &n, formats.data()));
    const VkFormat target_format =
        hdr ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_B8G8R8A8_UNORM;
    const VkColorSpaceKHR color_space =
        hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool found = false;
    for (auto f : formats)
        found |= f.format == target_format && f.colorSpace == color_space;
    if (!found)
        fail(hdr ? "HDR10 output unavailable on this display" : "BGRA8 SDR output unavailable");
    VkSwapchainCreateInfoKHR sc = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface_;
    sc.minImageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount && sc.minImageCount > caps.maxImageCount)
        fail("swapchain image count");
    sc.imageFormat = target_format;
    sc.imageColorSpace = color_space;
    sc.imageExtent = {3840, 2160};
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    sc.clipped = VK_TRUE;
    const VkResult swap_result = vkCreateSwapchainKHR(c_.device, &sc, nullptr, &swapchain_);
    if (swap_result != VK_SUCCESS)
        fail(hdr ? "HDR10 scanout could not initialize: verify TV HDR and PS5 HDR settings"
                 : "SDR scanout could not initialize");
    VK_OK(vkGetSwapchainImagesKHR(c_.device, swapchain_, &n, nullptr));
    images_.resize(n);
    views_.resize(n);
    rendered_.resize(n);
    VK_OK(vkGetSwapchainImagesKHR(c_.device, swapchain_, &n, images_.data()));
    VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_OK(vkCreateSemaphore(c_.device, &semaphore, nullptr, &acquired_));
    for (unsigned i = 0; i < n; ++i)
    {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = sc.imageFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_OK(vkCreateImageView(c_.device, &vi, nullptr, &views_[i]));
        VK_OK(vkCreateSemaphore(c_.device, &semaphore, nullptr, &rendered_[i]));
    }
    auto &conversion = renderer_.conversion;
    conversion.red_green[0] = hdr ? 1.4746f : 1.5748f;
    conversion.red_green[1] = hdr ? -0.164553f : -0.187324f;
    conversion.red_green[2] = hdr ? -0.571353f : -0.468124f;
    conversion.red_green[3] = hdr ? 512.0f / 1023.0f : 128.0f / 255.0f;
    conversion.blue_extent[0] = hdr ? 1.8814f : 1.8556f;
    conversion.blue_extent[3] = hdr ? 1.0f : 0.0f;
    conversion.range[0] = hdr ? 64.0f / 1023.0f : 16.0f / 255.0f;
    conversion.range[1] = hdr ? 1023.0f / 876.0f : 255.0f / 219.0f;
    conversion.range[2] = hdr ? 1023.0f / 896.0f : 255.0f / 224.0f;
    conversion.range[3] = tv_safe ? 1792.0f / 1920.0f : 1.0f;
    renderer_.initialize(c_, *output_, sc.imageFormat, views_);
    log_line("presentation initialized 3840x2160 %.3f Hz %s %s images=%u", refresh_,
             hdr ? "HDR10" : "SDR", vsync ? "FIFO" : "IMMEDIATE", n);
}
bool PyroWaveVideoBackend::ingest(const uint8_t *data,
                                  const std::vector<PyroWaveFraming::Span> &spans, bool partial)
{
    decoder_.reset_frame();
    for (const auto &span : spans)
        if (!decoder_.push_packet(data + span.offset, span.size))
            return false;
    return partial ? pyrowave_decoder_decode_is_ready_with_sideband(decoder_.native_handle(), true,
                                                                    0, 0.9f, nullptr, 0)
                   : decoder_.frame_ready();
}
VideoFrameTiming PyroWaveVideoBackend::present()
{
    double start = clock_ms();
    unsigned index = 0;
    VK_OK(vkAcquireNextImageKHR(c_.device, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE,
                                &index));
    c_.begin();
    output_->prepare();
    vkCmdResetQueryPool(c_.cmd, c_.queries, 0, 4);
    vkCmdWriteTimestamp(c_.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c_.queries, 0);
    if (!decoder_.decode(c_.cmd, *output_))
        fail("decode frame");
    vkCmdWriteTimestamp(c_.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c_.queries, 1);
    vkCmdWriteTimestamp(c_.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c_.queries, 2);
    renderer_.render(c_.cmd, *output_, index);
    vkCmdWriteTimestamp(c_.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c_.queries, 3);
    VK_OK(vkEndCommandBuffer(c_.cmd));
    VK_OK(vkResetFences(c_.device, 1, &c_.fence));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired_;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &c_.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &rendered_[index];
    VK_OK(vkQueueSubmit(c_.queue, 1, &submit, c_.fence));
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered_[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &index;
    VK_OK(vkQueuePresentKHR(c_.queue, &pi));
    ++requested_;
    VK_OK(vkWaitForFences(c_.device, 1, &c_.fence, VK_TRUE, 30000000000ull));
    uint64_t t[4] = {};
    VK_OK(vkGetQueryPoolResults(c_.device, c_.queries, 0, 4, sizeof(t), t, sizeof(uint64_t),
                                VK_QUERY_RESULT_64_BIT));
    uint64_t mask = c_.timestamp_bits == 64 ? ~uint64_t(0) : (uint64_t(1) << c_.timestamp_bits) - 1;
    double scale = c_.props.limits.timestampPeriod / 1e6;
    return {double((t[1] - t[0]) & mask) * scale, double((t[3] - t[2]) & mask) * scale,
            double((t[3] - t[0]) & mask) * scale, clock_ms() - start};
}
void PyroWaveVideoBackend::shutdown()
{
    if (!output_)
        return;
    (void)vkDeviceWaitIdle(c_.device);
    renderer_.shutdown();
    decoder_.shutdown();
    output_.reset();
    for (auto view : views_)
        vkDestroyImageView(c_.device, view, nullptr);
    views_.clear();
    for (auto sem : rendered_)
        vkDestroySemaphore(c_.device, sem, nullptr);
    rendered_.clear();
    if (acquired_)
        vkDestroySemaphore(c_.device, acquired_, nullptr);
    if (swapchain_)
        vkDestroySwapchainKHR(c_.device, swapchain_, nullptr);
    if (surface_)
        vkDestroySurfaceKHR(c_.instance, surface_, nullptr);
    acquired_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
}
