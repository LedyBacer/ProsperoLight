# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-side deterministic checks for playout reserve and VideoOut feedback."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PlayoutBudgetPolicy(unittest.TestCase):
    def test_budget_and_presentation_boundaries(self):
        code = r"""
#include "playout_budget.hpp"
#include <cassert>
int main() {
    moonlight::VideoPlayoutBudget p;
    auto lan=p.observe(0,1000000,1002000,60);
    assert(lan.remaining_us==0 && lan.spent_us==0);
    auto clean=p.observe(30000,1000000,1009000,60);
    assert(clean.remaining_us==30000 && clean.spent_us==0);
    auto bad=p.observe(30000,1000000,1035000,60);
    assert(bad.remaining_us==15000 && bad.spent_us==15000);
    auto poor=p.observe(30000,1000000,1019000,60);
    assert(poor.remaining_us<30000 && poor.remaining_us>=15000);
    auto invalid=p.observe(30000,2000000,1000000,60);
    assert(invalid.remaining_us==30000);
    assert(p.stats.observed==5 && p.stats.adjusted==2);
    assert(p.stats.max_saved_us==15000);
    assert(!moonlight::deadline_mailbox_replacement(100,101,1,2,99));
    assert(!moonlight::deadline_mailbox_replacement(200,100,2,2,99));
    assert(!moonlight::deadline_mailbox_replacement(200,100,1,2,201));
    assert(moonlight::deadline_mailbox_replacement(200,100,1,2,199));
    moonlight::SourceDueClock pts;
    uint64_t previous_due = 0;
    for (uint64_t frame = 1; frame <= 240; ++frame) {
        const uint64_t clock_pts = 1000000 + frame * 16666;
        const uint64_t ready = 2000000 + clock_pts + (frame % 17 == 0 ? 19000 : 0);
        const uint64_t due = pts.observe(clock_pts, ready, 16666);
        assert(due > 0 && pts.stats.cushion_us <= 16666);
        if (frame % 17 != 0 && previous_due)
            assert(due > previous_due);
        previous_due = due;
    }
    assert(pts.stats.samples == 240);
    const auto anchors = pts.stats.reanchors;
    pts.observe(10000000, 11000000, 16666);
    assert(pts.stats.reanchors == anchors + 1);
    moonlight::PresentFeedback feedback;
    feedback.submitted(1020,1015,1025); // Early by 5; 10us service.
    feedback.submitted(2000,2010,2050); // 10us scheduler overshoot.
    assert(feedback.stats.submitted==2 && feedback.stats.late_submissions==1);
    assert(feedback.stats.scheduler_late_total_us==10);
    assert(feedback.stats.submit_service_total_us==50);
    feedback.observed_flip(3000,2,true);
    feedback.observed_flip(13000,3,true);
    feedback.observed_flip(25000,3,true);
    assert(feedback.stats.completed==3);
    assert(feedback.stats.flip_counter_advances==1);
    assert(feedback.stats.repeated_flip_counters==1);
    assert(feedback.stats.observed_flip_interval_max_us==12000);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp)
            (p / "policy.cpp").write_text(code)
            exe = p / "policy"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "include"), str(p / "policy.cpp"),
                            "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
