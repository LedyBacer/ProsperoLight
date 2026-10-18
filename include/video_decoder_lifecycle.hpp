/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream-aware native video
 * decoding. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier:
 * GPL-3.0-or-later
 */
#pragma once
#include "video_stream_requirements.hpp"
namespace moonlight::video
{
enum class State
{
    Uninitialized,
    WaitingParameters,
    Configuring,
    Ready,
    Reconfiguring,
    Recovering,
    Failed,
    Stopping,
    Stopped
};
enum class Error
{
    None,
    Parameters,
    Unsupported,
    Capacity,
    Corruption,
    Reset,
    Allocation,
    Presentation,
    Exhausted
};
inline const char *name(State s)
{
    constexpr const char *names[]{"uninitialized", "waiting_parameters", "configuring",
                                  "ready",         "reconfiguring",      "recovering",
                                  "failed",        "stopping",           "stopped"};
    return names[static_cast<unsigned>(s)];
}
struct Lifecycle
{
    State state{State::Uninitialized};
    Error reason{Error::None};
    Requirements stream{};
    Plan effective{};
    uint32_t generation{}, reconfigurations{}, attempts{}, successes{}, failures{}, corruptions{};
    uint64_t waiting_since{}, initialization_us{}, reconfiguration_us{}, ready_us{},
        first_ready_us{};
    static constexpr uint32_t recovery_budget = 2, corruption_budget = 8;
    static constexpr uint64_t startup_timeout_us = 10000000;
    void wait(uint64_t now)
    {
        state = State::WaitingParameters;
        waiting_since = now;
    }
    bool expired(uint64_t now) const
    {
        return state == State::WaitingParameters && now >= waiting_since &&
               now - waiting_since >= startup_timeout_us;
    }
    bool changed(const Requirements &r) const
    {
        // Semantic configuration equality: replacement is unnecessary for a new
        // parameter identity with the same decoding requirements.
        return !stream.verified || r.codec != stream.codec || r.profile != stream.profile ||
               r.level != stream.level || r.width != stream.width || r.height != stream.height ||
               r.visible_width != stream.visible_width ||
               r.visible_height != stream.visible_height || r.chroma != stream.chroma ||
               r.bit_depth != stream.bit_depth || r.buffering != stream.buffering ||
               r.references != stream.references || r.reorder != stream.reorder;
    }
    void fail(Error e)
    {
        reason = e;
        if (state != State::Failed)
            ++failures;
        state = State::Failed;
    }
    bool recover(Error e)
    {
        if (state == State::Stopping || state == State::Stopped || state == State::Failed)
            return false;
        if (attempts >= recovery_budget)
        {
            fail(Error::Exhausted);
            return false;
        }
        ++attempts;
        reason = e;
        state = State::Recovering;
        return true;
    }
    void configured(const Requirements &r, Plan p, uint64_t now, uint64_t elapsed)
    {
        if (generation)
        {
            ++reconfigurations;
            reconfiguration_us += elapsed;
        }
        else
            initialization_us = now >= waiting_since ? now - waiting_since : elapsed;
        if (!generation)
            first_ready_us = now;
        stream = r;
        effective = p;
        ++generation;
        ready_us = now;
        state = State::Ready;
    }
    bool corruption()
    {
        if (++corruptions <= corruption_budget)
            return true;
        fail(Error::Exhausted);
        return false;
    }
};
} // namespace moonlight::video
