// ps5-homebrew-ui - WAV (RIFF PCM) decoding for sound effects.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hui::audio
{

struct DecodedWav
{
    std::vector<float> samples; // interleaved stereo at 48 kHz
    std::size_t frames = 0;
    std::string error;
    bool ok() const
    {
        return error.empty();
    }
};

// Accepts 48 kHz PCM, 16- or 24-bit, mono (duplicated to both channels) or
// stereo. Anything else is rejected with an error naming the problem.
DecodedWav decode_wav(std::string_view data);

} // namespace hui::audio
