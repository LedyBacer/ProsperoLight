// SPDX-License-Identifier: MIT
#pragma once
#include "../vulkan/vulkan_context.hpp"
struct PlaneConversion {
    float red_green[4] = {1.5748f,-0.187324f,-0.468124f,512.0f/1023.0f};
    float blue_extent[4] = {1.8556f,3840.0f,2160.0f,0};
    float range[4] = {16.0f/255.0f,255.0f/219.0f,255.0f/224.0f,1.0f};
};
// Consumes decoded planes; has no compressed-frame, file or VideoOut logic.
class PyroWaveRenderer {
public:
    void initialize(VulkanContext &context, const Output &planes, VkFormat format,
                    const std::vector<VkImageView> &targets);
    void render(VkCommandBuffer cmd, const Output &planes, unsigned target);
    void update_hud(const char *text, bool enabled);
    void shutdown();
    ~PyroWaveRenderer() { shutdown(); }
    PlaneConversion conversion;
private:
    VulkanContext *c_ = nullptr;
    VkBuffer hud_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory hud_memory_ = VK_NULL_HANDLE;
    uint32_t *hud_mapped_ = nullptr;
    bool hud_coherent_ = false;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageView planes_[3] = {};
    std::vector<VkFramebuffer> framebuffers_;
};
