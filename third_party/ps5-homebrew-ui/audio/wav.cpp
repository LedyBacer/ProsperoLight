// ps5-homebrew-ui - WAV (RIFF PCM) decoding for sound effects.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/wav.hpp"

#include "audio/mixer.hpp"

#include <cstdint>
#include <cstring>

namespace hui::audio
{

namespace
{

std::uint32_t u32(std::string_view data, std::size_t offset)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24;
}

std::uint16_t u16(std::string_view data, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset]) |
                                      static_cast<unsigned char>(data[offset + 1]) << 8);
}

float pcm(std::string_view data, std::size_t offset, int bits)
{
    if (bits == 16)
        return static_cast<float>(static_cast<std::int16_t>(u16(data, offset))) / 32768.0f;
    std::int32_t value =
        static_cast<std::int32_t>(static_cast<unsigned char>(data[offset]) |
                                  static_cast<unsigned char>(data[offset + 1]) << 8 |
                                  static_cast<unsigned char>(data[offset + 2]) << 16);
    if ((value & 0x800000) != 0)
        value -= 0x1000000;
    return static_cast<float>(value) / 8388608.0f;
}

} // namespace

DecodedWav decode_wav(std::string_view data)
{
    DecodedWav result;
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0)
    {
        result.error = "not a RIFF/WAVE file";
        return result;
    }
    int channels = 0;
    int bits = 0;
    std::uint32_t rate = 0;
    bool have_format = false;
    std::string_view pcm_data;
    std::size_t offset = 12;
    while (offset + 8 <= data.size())
    {
        const std::string_view id = data.substr(offset, 4);
        const std::uint32_t size = u32(data, offset + 4);
        const std::size_t body = offset + 8;
        if (size > data.size() - body)
        {
            result.error = "truncated chunk";
            return result;
        }
        if (id == "fmt " && size >= 16)
        {
            const std::uint16_t format = u16(data, body);
            channels = u16(data, body + 2);
            rate = u32(data, body + 4);
            bits = u16(data, body + 14);
            // 0xFFFE (extensible) carries PCM in its subformat GUID.
            if (format != 1 && format != 0xfffe)
            {
                result.error = "not PCM";
                return result;
            }
            have_format = true;
        }
        else if (id == "data")
        {
            pcm_data = data.substr(body, size);
        }
        offset = body + size + (size & 1u);
    }
    if (!have_format || pcm_data.empty())
    {
        result.error = "missing fmt or data chunk";
        return result;
    }
    if (rate != static_cast<std::uint32_t>(kSampleRate))
    {
        result.error = "sample rate must be 48000 Hz";
        return result;
    }
    if (channels != 1 && channels != 2)
    {
        result.error = "must be mono or stereo";
        return result;
    }
    if (bits != 16 && bits != 24)
    {
        result.error = "must be 16- or 24-bit";
        return result;
    }
    const std::size_t bytes_per_sample = static_cast<std::size_t>(bits / 8);
    const std::size_t frame_bytes = bytes_per_sample * static_cast<std::size_t>(channels);
    result.frames = pcm_data.size() / frame_bytes;
    result.samples.resize(result.frames * 2);
    for (std::size_t frame = 0; frame < result.frames; ++frame)
    {
        const std::size_t base = frame * frame_bytes;
        const float left = pcm(pcm_data, base, bits);
        const float right = channels == 2 ? pcm(pcm_data, base + bytes_per_sample, bits) : left;
        result.samples[frame * 2] = left;
        result.samples[frame * 2 + 1] = right;
    }
    return result;
}

} // namespace hui::audio
