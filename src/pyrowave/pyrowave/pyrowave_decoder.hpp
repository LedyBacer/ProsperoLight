// SPDX-License-Identifier: MIT
#pragma once
#include "../vulkan/vulkan_context.hpp"

// Packet source and presentation are deliberately outside this component.
// Precision is selected by output plane format; PyroWave has no 10-bit flag.
class PyroWaveDecoder {
public:
    PyroWaveDecoder() = default;
    PyroWaveDecoder(const PyroWaveDecoder &) = delete;
    PyroWaveDecoder &operator=(const PyroWaveDecoder &) = delete;
    ~PyroWaveDecoder() { shutdown(); }
    bool initialize(VulkanContext &context, unsigned width, unsigned height, bool chroma444);
    void reset_frame();
    bool push_packet(const void *data, size_t size);
    bool frame_ready() const;
    // Caller owns layout transitions, command submission and completion.
    bool decode(VkCommandBuffer cmd, Output &output);
    void shutdown();
    pyrowave_decoder native_handle() const { return decoder_; }
private:
    VulkanContext *context_ = nullptr;
    pyrowave_decoder decoder_ = nullptr;
};
