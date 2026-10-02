// ps5-homebrew-ui - Little-endian byte writer/reader for save payloads.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hui::bytes
{

class Writer
{
  public:
    template <typename T> void put(T value)
    {
        for (std::size_t i = 0; i < sizeof(T); ++i)
            out_.push_back(
                static_cast<char>((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xff));
    }
    void put_bool(bool value)
    {
        put<std::uint8_t>(value ? 1 : 0);
    }
    const std::string &data() const
    {
        return out_;
    }

  private:
    std::string out_;
};

// Reads fields in order; any overrun marks the reader failed and yields 0.
class Reader
{
  public:
    explicit Reader(std::string_view data) : data_(data)
    {
    }
    template <typename T> T get()
    {
        if (data_.size() - position_ < sizeof(T) || position_ > data_.size())
        {
            ok_ = false;
            return T{};
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data_[position_ + i]))
                     << (8 * i);
        position_ += sizeof(T);
        return static_cast<T>(value);
    }
    bool get_bool()
    {
        return get<std::uint8_t>() != 0;
    }
    bool ok() const
    {
        return ok_;
    }
    bool finished() const
    {
        return ok_ && position_ == data_.size();
    }

  private:
    std::string_view data_;
    std::size_t position_ = 0;
    bool ok_ = true;
};

} // namespace hui::bytes
