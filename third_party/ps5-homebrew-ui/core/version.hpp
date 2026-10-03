// ps5-homebrew-ui - The app version, read from the packaged param.json.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace hui
{

// The "contentVersion" value (PlayStation NN.NNN.NNN, e.g. "01.000.000") from
// param.json text, or "" when it is missing or malformed. Release tags use the
// same string, so what the app shows matches the GitHub release.
std::string content_version(std::string_view param_json);

// Reads param.json from path; "" when it cannot be read.
std::string read_content_version(const std::string &path);

} // namespace hui
