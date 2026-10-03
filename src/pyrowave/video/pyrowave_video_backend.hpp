// SPDX-License-Identifier: MIT
#pragma once
#include "pyrowave_renderer.hpp"
#include "../pyrowave/pyrowave_decoder.hpp"
#include <memory>
#include "../pyrowaveframing.h"
struct VideoFrameTiming
{
    double decode_ms, render_ms, total_ms, cpu_ms;
};
class PyroWaveVideoBackend
{
  public:
    explicit PyroWaveVideoBackend(VulkanContext &context) : c_(context)
    {
    }
    void initialize(unsigned width, unsigned height, unsigned fps, bool chroma444, bool hdr,
                    bool vsync, bool tv_safe);
    bool ingest(const uint8_t *data, const std::vector<PyroWaveFraming::Span> &spans, bool partial);
    VideoFrameTiming present(void (*before_present)(void *) = nullptr, void *context = nullptr,
                             bool wait_for_prepared = true);
    void update_hud(const char *text, bool enabled)
    {
        renderer_.update_hud(text, enabled);
    }
    void shutdown();
    ~PyroWaveVideoBackend()
    {
        shutdown();
    }
    double refresh_hz() const
    {
        return refresh_;
    }
    uint64_t requested() const
    {
        return requested_;
    }

  private:
    VulkanContext &c_;
    PyroWaveDecoder decoder_;
    std::unique_ptr<Output> output_;
    PyroWaveRenderer renderer_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> rendered_;
    double refresh_ = 0;
    uint64_t requested_ = 0;
};
