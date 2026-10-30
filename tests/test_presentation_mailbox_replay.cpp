/*
 * ps5-native-app-boilerplate / ProsperoLight - deterministic mailbox/pacer replay.
 * Copyright (C) 2026 LedyBacer and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "frame_pacing.hpp"
#include "moonlight_pipeline.hpp"
#include "playout_budget.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

struct Picture
{
    uint32_t frame{};
    uint64_t pts_us{}, ready_us{};
};

struct Result
{
    uint64_t submissions{}, capacity_drops{}, age_drops{}, catchups{};
    uint64_t max_gap_us{}, last_frame{}, total_ready_age_us{};
};

// This deliberately models the receive/ready/present scheduling boundaries,
// not the PS5 GPU execution or the physical display scanout. Every decision
// uses the same mailbox, admission guard and pacer as the application.
static Result replay(unsigned source_fps, unsigned refresh_x100, bool burst, bool catchup = true)
{
    constexpr uint64_t base_us = UINT64_C(1000000);
    std::vector<Picture> arrival;
    uint64_t preceding = 0;
    for (uint32_t index = 1; index <= 360; ++index)
    {
        const uint64_t pts = UINT64_C(1000000) * index / source_fps;
        const uint64_t spike = burst && (index % 11 == 0 || index % 11 == 1) ? 24000 : 0;
        const uint64_t ready = std::max(base_us + pts + spike + 1000, preceding + 1);
        arrival.push_back({index, pts, ready});
        preceding = ready;
    }

    moonlight::ReadyMailbox<Picture> mailbox;
    mailbox.capacity = 2;
    moonlight::FramePacing pacer;
    pacer.reset(source_fps);
    pacer.set_network_jitter_budget_us(burst ? 30000 : 0);
    Result result{};
    uint64_t now = base_us, last_submit = 0, last_flip = 0;
    size_t input = 0;

    const auto admit = [&](const Picture &next)
    {
        Picture displaced{};
        if (mailbox.publish(next, &displaced))
        {
            assert(displaced.frame < next.frame);
            ++result.capacity_drops;
        }
    };

    while (input < arrival.size() || mailbox.full)
    {
        if (!mailbox.full && input < arrival.size())
            now = std::max(now, arrival[input].ready_us);
        while (input < arrival.size() && arrival[input].ready_us <= now)
            admit(arrival[input++]);
        Picture current{};
        if (!mailbox.take(&current))
            continue;
        const uint64_t admission_due = current.ready_us + pacer.admission_limit_us(0, 0);
        while (mailbox.full &&
               moonlight::deadline_mailbox_replacement(now, admission_due, current.pts_us,
                                                       mailbox.item.pts_us, mailbox.item.ready_us))
        {
            ++result.age_drops;
            assert(mailbox.take(&current));
        }

        uint64_t deadline =
            pacer.target(current.frame, current.pts_us, now, refresh_x100, last_flip);
        if (burst && catchup && mailbox.full)
            deadline = pacer.catch_up_deadline(current.ready_us, now, deadline, refresh_x100);
        // Producer can publish a successor during the <=1 ms present-thread
        // polling. Advance the virtual clock to each actual ready event.
        while (input < arrival.size() && arrival[input].ready_us < deadline)
        {
            now = arrival[input].ready_us;
            admit(arrival[input++]);
            if (burst && catchup && mailbox.full)
                deadline = pacer.catch_up_deadline(current.ready_us, now, deadline, refresh_x100);
        }

        now = std::max(now, deadline);
        assert(current.frame > result.last_frame);
        const uint64_t previous_submit = last_submit;
        const uint64_t previous_flip = last_flip;
        // The real firmware completion timestamp is unknown to the host.
        // A virtual vblank floor identifies impossible sub-period scheduling;
        // it does not claim that the GPU displayed the frame in that slot.
        const uint64_t display_period = UINT64_C(100000000) / refresh_x100;
        const uint64_t virtual_flip =
            ((now + display_period - 1) / display_period) * display_period;
        last_flip = std::max(virtual_flip, previous_flip + (previous_flip ? display_period : 0));
        pacer.submitted(deadline, now, now >= current.ready_us ? now - current.ready_us : 0);
        if (previous_submit)
        {
            assert(now >= previous_submit);
            result.max_gap_us = std::max(result.max_gap_us, now - previous_submit);
        }
        last_submit = now;
        result.last_frame = current.frame;
        result.total_ready_age_us += now - current.ready_us;
        ++result.submissions;
        // Completion before the next selection prevents fake infinite GPU
        // throughput from clearing the mailbox instantaneously.
        now = last_flip;
    }
    result.catchups = pacer.stats.backlog_catchups;
    assert(result.submissions + result.capacity_drops + result.age_drops == arrival.size());
    assert(result.max_gap_us < 300000);
    assert(pacer.stats.reserve_us <= 30000);
    return result;
}

int main()
{
    const auto lan60 = replay(60, 6000, false);
    assert(lan60.catchups == 0 && lan60.submissions > 300);
    const auto old_wan60 = replay(60, 6000, true, false);
    const auto wan60 = replay(60, 6000, true);
    assert(wan60.catchups > 0 && wan60.submissions > 100);
    assert(old_wan60.catchups == 0);
    // Catch-up is a latency-containment mechanism. It must not cost more
    // ready-to-submit residence under exactly the same burst sequence.
    assert(wan60.total_ready_age_us * old_wan60.submissions <=
           old_wan60.total_ready_age_us * wan60.submissions);
    const auto fractional = replay(60, 5994, true);
    assert(fractional.submissions > 100);
    const auto high_refresh = replay(60, 11988, true);
    assert(high_refresh.submissions > 100);
    // A 50 FPS source on a 60 Hz panel is not a fixed-rate divisor and must
    // remain on the established off-divisor presentation policy.
    const auto host_limited = replay(50, 6000, true);
    assert(host_limited.catchups == 0 && host_limited.submissions > 100);
    return 0;
}
