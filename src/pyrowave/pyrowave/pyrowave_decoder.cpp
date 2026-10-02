// SPDX-License-Identifier: MIT
#include "pyrowave_decoder.hpp"
bool PyroWaveDecoder::initialize(VulkanContext &c, unsigned width, unsigned height, bool chroma444) {
    shutdown();
    pyrowave_decoder_create_info info = {};
    info.device = c.pyro; info.width = width; info.height = height;
    info.chroma = chroma444 ? PYROWAVE_CHROMA_SUBSAMPLING_444 : PYROWAVE_CHROMA_SUBSAMPLING_420; info.fragment_path = false;
    if (pyrowave_decoder_create(&info, &decoder_) != PYROWAVE_SUCCESS) return false;
    context_ = &c; return true;
}
void PyroWaveDecoder::reset_frame() { pyrowave_decoder_clear(decoder_); }
bool PyroWaveDecoder::push_packet(const void *data, size_t size) {
    return decoder_ && pyrowave_decoder_push_packet(decoder_, data, size) == PYROWAVE_SUCCESS;
}
bool PyroWaveDecoder::frame_ready() const {
    return decoder_ && pyrowave_decoder_decode_is_ready(decoder_, false);
}
bool PyroWaveDecoder::decode(VkCommandBuffer cmd, Output &output) {
    if (!decoder_ || !cmd || !context_) return false;
    pyrowave_device_set_command_buffer(context_->pyro, cmd);
    auto r = pyrowave_decoder_decode_gpu_buffer(decoder_, nullptr, nullptr, &output.views);
    pyrowave_device_set_command_buffer(context_->pyro, VK_NULL_HANDLE);
    return r == PYROWAVE_SUCCESS;
}
void PyroWaveDecoder::shutdown() {
    if (decoder_) pyrowave_decoder_destroy(decoder_);
    decoder_ = nullptr; context_ = nullptr;
}
