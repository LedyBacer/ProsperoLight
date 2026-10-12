// SPDX-License-Identifier: MIT
#pragma once
#include "pyrowave_renderer.hpp"
#include "../pyrowave/pyrowave_decoder.hpp"
#include <memory>
#include "../pyrowaveframing.h"
struct VideoFrameTiming
{
    double decode_ms{}, render_ms{}, total_ms{}, cpu_ms{};
    double acquire_ms{}, record_ms{}, submit_ms{}, prepared_wait_ms{}, pacing_ms{}, present_ms{},
        completion_ms{};
    bool repeat_skipped{}, repeat_superseded{};
};
class PyroWaveVideoBackend
{
  public:
    explicit PyroWaveVideoBackend(VulkanContext &context) : c_(context)
    {
    }
    void initialize(unsigned width, unsigned height, unsigned fps, bool chroma444, bool hdr,
                    bool vsync, bool tv_safe, unsigned pacing);
    bool ingest(const uint8_t *data, const std::vector<PyroWaveFraming::Span> &spans, bool partial);
    VideoFrameTiming present(void (*before_present)(void *) = nullptr, void *context = nullptr,
                             bool wait_for_prepared = true, bool repeat = false,
                             bool (*admit_repeat)(void *) = nullptr);
    void update_hud(const native_agc_metrics_t *metrics, bool enabled)
    {
        renderer_.update_hud(metrics, enabled);
    }
    bool hud_visible() const
    {
        return renderer_.hud_visible();
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
