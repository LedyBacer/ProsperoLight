# ps5-native-app-boilerplate / ProsperoLight - Source-clock pacing and bounded ready queue regression checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class FramePacingPolicy(unittest.TestCase):
    def test_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'pacing.cpp'
            source.write_text(r'''
#include "frame_pacing.hpp"
#include "moonlight_pipeline.hpp"
#include <cassert>
#include <cstring>
int main() {
    for(uint32_t unpeg: {0u,0x8029001cu,0x80290012u}) {
        const bool released=moonlight::output_released(unpeg);
        assert(released==(unpeg!=0x80290012u));
        assert(!moonlight::variable_after_peg(released,0));
        assert(!moonlight::variable_after_peg(released,0x80290012u));
        assert(moonlight::variable_after_peg(released,0x8029001cu)==released);
    }
    moonlight::VrrRepeatPolicy telemetry;
    telemetry.reset(60);
    telemetry.presented(1000000);
    telemetry.repeated(1020000);
    telemetry.waited(1019000,1019500,1020000);
    assert(telemetry.stats.pictures==1 && telemetry.stats.repeats==1);
    assert(telemetry.stats.gap_max_us==20000 && telemetry.stats.wait_total_us==1000);
    assert(telemetry.stats.late_max_us==500);
    moonlight::SourceTimestamp clock;
    const auto before=clock.update(0xfffffff0u,0);
    const auto after=clock.update(734u,0);
    assert(after>before && after-before>=8333 && after-before<=8334);
    assert(clock.update(734u,42)==42);
    moonlight::ReadyMailbox<int> q;
    q.capacity=2;
    int displaced=0, out=0;
    assert(!q.publish(1,&displaced) && !q.publish(2,&displaced));
    assert(q.publish(3,&displaced) && displaced==1);
    assert(q.take(&out) && out==2);
    assert(q.take(&out) && out==3 && !q.full);
    q.capacity=1;
    assert(!q.publish(4,&displaced));
    assert(q.publish(5,&displaced) && displaced==4);
    assert(q.take(&out) && out==5);
    for(unsigned fps: {30u,60u,75u,90u,120u}) {
        moonlight::FramePacing p;
        p.reset(fps);
        uint64_t previous=0;
        for(int frame=1;frame<1000;frame++) {
            const uint64_t pts=uint64_t(frame)*1000000/fps;
            const uint64_t jitter=frame%53==0?11000:frame%3*300;
            uint64_t ready=1000000+pts+jitter;
            if(frame>1 && ready<previous) ready=previous;
            auto target=p.target(frame,pts,ready);
            assert(target>=ready);
            if(previous) assert(target>=previous+uint64_t(1000000/fps)*97/100);
            assert(p.stats.reserve_us<=10000 && p.stats.reserve_us<=p.stats.period_us);
            p.submitted(target,target, target-ready);
            previous=target;
        }
        // Timestamp discontinuity and long host stall recover without an
        // unbounded backlog, and the only remaining image can still be shown.
        auto target=p.target(2000,1,previous+2000000);
        assert(target>=previous+2000000 && target<=previous+2010000);
        assert(p.stats.resets);
    }
    // Nonary/Punktfunk-inspired burst_pacer catch-up: a decoded successor that
    // arrives during an existing WAN reserve can shorten that wait, but may
    // never evade the source-period floor for fixed VSync.
    moonlight::FramePacing burst_pacer;
    burst_pacer.reset(60);
    burst_pacer.set_network_jitter_budget_us(30000);
    burst_pacer.submitted(1000000,1000000,0);
    auto burst_target=burst_pacer.target(2,33333,1016666,6000,1000000);
    const auto original_burst_target=burst_target+20000;
    const auto caught_up=burst_pacer.catch_up_deadline(1016666,1016666,original_burst_target,6000);
    assert(caught_up>=1016666 && caught_up>=1000000+16666*98/100);
    assert(caught_up<original_burst_target);
    assert(burst_pacer.stats.backlog_catchups==1);
    assert(burst_pacer.stats.backlog_saved_us==original_burst_target-caught_up);
    // A second observation of the same successor must not double-account.
    assert(burst_pacer.catch_up_deadline(1016666,1016666,caught_up,6000)==caught_up);
    assert(burst_pacer.stats.backlog_catchups==1);
    // When the source cadence is not a divisor of the fixed display grid
    // (90 FPS on 60 Hz), its fractional presentation policy must be retained.
    assert(burst_pacer.catch_up_deadline(1016666,1016666,original_burst_target,9000)==original_burst_target);
    burst_pacer.set_network_jitter_budget_us(0);
    assert(burst_pacer.catch_up_deadline(1016666,1016666,original_burst_target,6000)==original_burst_target);
    moonlight::FramePacing initial;
    initial.reset(60);
    initial.set_network_jitter_budget_us(30000);
    assert(initial.catch_up_deadline(1016666,1016666,original_burst_target,6000)==original_burst_target);
    // A scheduler that wakes after the floor has passed must not invent
    // saved waiting or report that the stream missed a target in the past.
    const uint64_t resumed_now=1019500;
    const uint64_t still_future=original_burst_target+1000;
    burst_pacer.set_network_jitter_budget_us(30000);
    const auto previous_catchups=burst_pacer.stats.backlog_catchups;
    const auto previous_saved=burst_pacer.stats.backlog_saved_us;
    assert(burst_pacer.catch_up_deadline(1016666,resumed_now,still_future,6000)
           ==resumed_now);
    assert(burst_pacer.stats.backlog_saved_us==previous_saved+still_future-resumed_now);
    assert(burst_pacer.stats.backlog_catchups==previous_catchups+1);
    const auto after_catchups=burst_pacer.stats.backlog_catchups;
    const auto after_saved=burst_pacer.stats.backlog_saved_us;
    assert(burst_pacer.catch_up_deadline(1016666,still_future+1000,still_future,6000)
           ==still_future);
    assert(burst_pacer.stats.backlog_catchups==after_catchups);
    assert(burst_pacer.stats.backlog_saved_us==after_saved);

    // Deterministic scheduler replay. Packet bursts become ready pictures,
    // an early successor arrives during the old playout reserve, and each
    // synthetic wake advances monotonically. 59.94 and 119.88 Hz represent
    // the real nominal PS5 output modes (in hundredths of a hertz).
    for(unsigned refresh : {5994u,11988u}) {
        for(bool impaired : {false,true}) {
            moonlight::FramePacing replay;
            replay.reset(60);
            uint64_t last_submit=0,previous_arrival=0;
            uint64_t accelerated=0;
            const uint64_t refresh_period=UINT64_C(100000000)/refresh;
            const uint64_t matched=refresh==11988?2*refresh_period:refresh_period;
            const uint64_t floor=matched*98/100;
            for(int frame=1;frame<=180;++frame) {
                const uint64_t pts=uint64_t(frame)*1000000/60;
                const uint64_t jitter=impaired && frame%11==0?22000:0;
                const uint64_t arrival=std::max(UINT64_C(1000000)+pts+jitter,previous_arrival+1);
                previous_arrival=arrival;
                replay.set_network_jitter_budget_us(impaired && frame>24?25000:0);
                const uint64_t base=replay.target(frame,pts,arrival,refresh,last_submit);
                const uint64_t old_deadline=impaired && frame>24
                    ?std::max(base,arrival+12000):base;
                const uint64_t successor_at=arrival+4000;
                uint64_t deadline=old_deadline;
                // Model the present-thread's existing 1ms polling.
                for(uint64_t now=arrival;now<deadline;now+=1000) {
                    if(now>=successor_at) {
                        const uint64_t changed=replay.catch_up_deadline(
                            arrival,now,deadline,refresh);
                        assert(changed>=now && changed>=arrival);
                        if(changed<deadline) ++accelerated;
                        deadline=changed;
                    }
                }
                const uint64_t submit=std::max(arrival,deadline);
                if(last_submit)
                    assert(submit>=last_submit+floor);
                replay.submitted(deadline,submit,submit-arrival);
                last_submit=submit;
            }
            if(impaired) {
                assert(accelerated>0);
                assert(replay.stats.backlog_catchups==accelerated);
                assert(replay.stats.backlog_saved_us>0);
            } else {
                assert(accelerated==0);
                assert(replay.stats.backlog_catchups==0);
                assert(replay.stats.backlog_saved_us==0);
            }
        }
    }
    // Slow decode/GPU preparation on an otherwise stable LAN must NOT
    // authorize a 30 ms playout reserve (ready time is not network evidence).
    moonlight::VideoArrivalJitter stable_network;
    moonlight::FramePacing slow_decoder;
    slow_decoder.reset(60);
    for(int frame=1;frame<=180;++frame) {
        const auto source=uint64_t(frame)*1000000/60;
        const auto arrival=1000000+source;
        stable_network.observe(frame,frame*1500,arrival);
        slow_decoder.set_network_jitter_budget_us(stable_network.budget_us());
        const auto ready=arrival+(frame%7==0?25000:1000);
        const auto target=slow_decoder.target(frame,source,ready);
        slow_decoder.submitted(target,target,target-ready);
        assert(slow_decoder.stats.reserve_us<=10000);
    }
    assert(stable_network.budget_us()==0 && stable_network.bursts()==0);
    // Repeated *arrival* bursts, close together, earn bounded extra reserve.
    // Arrival is monotonic, even when a delayed burst is followed by catchup.
    moonlight::VideoArrivalJitter impaired_network;
    moonlight::FramePacing unstable;
    unstable.reset(60);
    uint64_t peak_reserve=0;
    for(int frame=1; frame<=160; ++frame) {
        const uint64_t pts=uint64_t(frame)*1000000/60;
        const uint64_t jitter=frame%7==0?25000:0;
        static uint64_t last_arrival=0;
        const uint64_t arrival=std::max(1000000+pts+jitter,last_arrival+1);
        last_arrival=arrival;
        impaired_network.observe(frame,frame*1500,arrival);
        unstable.set_network_jitter_budget_us(impaired_network.budget_us());
        const uint64_t ready=arrival+1000;
        const auto target=unstable.target(frame,pts,ready);
        assert(target>=ready && target<=ready+30000);
        unstable.submitted(target,target,target-ready);
        peak_reserve=std::max(peak_reserve,unstable.stats.reserve_us);
    }
    assert(peak_reserve>10000 && peak_reserve<=30000);
    assert(impaired_network.peak_us()>10000 && impaired_network.bursts()>0);
    // A missing video frame must not erase protection learned during recent
    // jitter. The estimator must rejoin consecutive pairs without treating
    // their larger source clock delta as new measured jitter.
    moonlight::VideoArrivalJitter lost_frame_network;
    uint64_t lost_last=0;
    for(int f=1;f<=100;++f) {
        const uint64_t pts=uint64_t(f)*1000000/60;
        const uint64_t jitter=f%7==0?25000:0;
        const uint64_t arrival=std::max(1000000+pts+jitter,lost_last+1);
        lost_last=arrival;
        lost_frame_network.observe(f,f*1500,arrival);
    }
    const auto before_gap=lost_frame_network.budget_us();
    assert(before_gap>10000);
    lost_frame_network.observe(104,104*1500,lost_last+66667);
    assert(lost_frame_network.budget_us()==before_gap);
    assert(lost_frame_network.missing_frames()==3);
    assert(lost_frame_network.discontinuities()>0);
    lost_frame_network.observe(200,200*1500,lost_last+10000000);
    assert(lost_frame_network.budget_us()==0); // expired evidence after long idle
    // Matching 60-on-60 fixed refresh normally submits immediately, but a
    // confirmed WAN burst is allowed a bounded reserve on that path too.
    moonlight::FramePacing fixed_wan;
    fixed_wan.reset(60);
    moonlight::VideoArrivalJitter fixed_arrivals;
    uint64_t fixed_last=0, fixed_arrival=0;
    bool fixed_extended=false;
    for(int frame=1;frame<=150;++frame) {
        const uint64_t source=uint64_t(frame)*1000000/60;
        const uint64_t spike=frame%7==0?25000:0;
        fixed_arrival=std::max(1000000+source+spike,fixed_arrival+1);
        fixed_arrivals.observe(frame,frame*1500,fixed_arrival);
        fixed_wan.set_network_jitter_budget_us(fixed_arrivals.budget_us());
        const uint64_t ready=fixed_arrival+1000;
        const auto target=fixed_wan.target(frame,source,ready,6000,fixed_last);
        assert(target>=ready && target<=ready+50000);
        if(fixed_arrivals.budget_us() && target>ready+1000)
            fixed_extended=true;
        fixed_wan.submitted(target,target,target-ready);
        fixed_last=target;
    }
    assert(fixed_extended);
    // The enlarged budget expires when the last burst has passed. RTP
    // discontinuity must drop it immediately, including rollover-safe deltas.
    uint64_t last_good=1000000+160*16667+30000;
    for(int frame=161;frame<=460;++frame) {
        last_good+=16667;
        impaired_network.observe(frame,frame*1500,last_good);
    }
    assert(impaired_network.budget_us()==0);
    impaired_network.observe(999,1500,9000000);
    assert(impaired_network.budget_us()==0);
    moonlight::VideoArrivalJitter rollover;
    rollover.observe(1,0xfffffff0u,1000000);
    rollover.observe(2,1484u,1016666);
    assert(rollover.bursts()==0 && rollover.budget_us()==0);
    // Capture jitter alternates long and short PTS deltas for twenty minutes.
    // The source is still 60 FPS; cadence must not drift into a 40 FPS cap.
    moonlight::FramePacing jittered;
    jittered.reset(60);
    uint64_t previous_jittered=0;
    for(int frame=1;frame<72000;frame++) {
        uint64_t source=uint64_t(frame)*1000000/60+(frame%2?10000:0);
        uint64_t ready=1000000+uint64_t(frame)*1000000/60;
        auto target=jittered.target(frame,source,ready);
        jittered.submitted(target,target,target-ready);
        previous_jittered=target;
    }
    assert(previous_jittered>0);
    assert(jittered.stats.period_us>16500 && jittered.stats.period_us<16800);
    // Idle repetition must not steal the next regular source frame; once
    // active it submits before the next display tick (60 Hz floor for VRR).
    assert(moonlight::idle_scanout_delay_us(120,11988,false,false)==16666);
    assert(moonlight::idle_scanout_delay_us(60,11988,false,false)==33333);
    assert(moonlight::idle_scanout_delay_us(30,11988,true,false)==16000);
    assert(moonlight::idle_scanout_delay_us(120,11988,true,true)==16000);
    assert(moonlight::idle_scanout_delay_us(120,11988,false,true)==4170);
    assert(moonlight::idle_scanout_delay_us(0,0,false,false)==33333);
    assert(!moonlight::idle_repeat_ready(0,0,true));
    assert(!moonlight::idle_repeat_ready(2,1,true));
    assert(!moonlight::idle_repeat_ready(1,UINT64_MAX,true));
    assert(!moonlight::idle_repeat_ready(2,2,false));
    assert(moonlight::idle_repeat_ready(2,2,true));
    // Simulate asynchronous scanout and a 60 FPS source on a 120 Hz link.
    // A repeat must not fill the intervening display tick ahead of the next
    // real frame. On idle, each repeat must retire before another is queued.
    const auto delay=moonlight::idle_scanout_delay_us(60,11988,true,false);
    assert(8333<delay && 16666<8333+delay);
    uint64_t requested=1, shown=0;
    assert(!moonlight::idle_repeat_ready(requested,shown,true));
    shown=1;
    assert(moonlight::idle_repeat_ready(requested,shown,true));
    ++requested;
    assert(!moonlight::idle_repeat_ready(requested,shown,true));
    // With no fresh pictures, idle repeats stay above 60 Hz; the very first
    // idle repeat still lets a 60 FPS real picture through.
    for (unsigned rate : {60u, 90u, 120u}) {
        moonlight::VrrRepeatPolicy idle;
        idle.reset(rate);
        idle.presented(1000000);
        assert(idle.interval()==16000);
        if (rate==60) assert(idle.idle_deadline()>1016666);
        const auto first=idle.idle_deadline();
        idle.repeated(first);
        assert(idle.idle_deadline()==first+16000);
        for (int i=0;i<20;++i) {
            const auto due=idle.idle_deadline();
            idle.repeated(due);
            assert(idle.idle_deadline()==due+16000);
        }
    }
    moonlight::RepeatRetryPolicy retry;
    assert(retry.deadline(0)==0);
    retry.failed(1000000);
    assert(retry.deadline(0)==1005000);
    retry.failed(1005000);
    assert(retry.deadline(0)==1015000);
    for (int i=0;i<20;++i) retry.failed(1100000+uint64_t(i)*250000);
    assert(retry.failures()<=7);
    retry.succeeded();
    assert(retry.failures()==0 && retry.deadline(0)==0);
    // 119.88 Hz output floor; cap genuine fresh frames without a fictitious
    // 120.00 Hz capability. LFC leaves deadline headroom for delayed frames.
    moonlight::VrrRepeatPolicy hfr;
    hfr.reset(120);
    hfr.configure(1,11988);
    assert(hfr.display_floor_us()>=8342);
    hfr.presented(1000000);
    assert(hfr.picture_target(1001000)>=1000000+hfr.display_floor_us());
    for(unsigned fps : {16u,20u,30u,35u,40u,50u,51u}) {
        moonlight::VrrRepeatPolicy lfc;
        lfc.reset(fps);
        lfc.configure(1,11988);
        assert(lfc.compensating());
        assert(lfc.interval()>=lfc.display_floor_us());
        assert(lfc.interval()<=16000);
        lfc.presented(1000000);
        auto last=uint64_t(1000000);
        // Until the next source picture arrives, a repeat must not violate
        // the watchdog, nor send faster than the VideoOut 119.88 Hz floor.
        for(unsigned i=0;i<lfc.repeat_factor()+1;i++) {
            auto due=lfc.idle_deadline();
            assert(due>=last+lfc.display_floor_us());
            assert(due<=last+16000 || due==lfc.deadline());
            lfc.repeated(due);
            last=due;
        }
    }
    moonlight::VrrRepeatPolicy slow;
    slow.reset(30);
    slow.configure(1,11988);
    assert(slow.repeat_factor()==3); // 30 -> 90: room for a genuine 33 ms frame
    slow.presented(1000000);
    slow.repeated(slow.idle_deadline());
    slow.repeated(slow.idle_deadline());
    assert(slow.idle_deadline()>=1033333);
    moonlight::VrrRepeatPolicy catchup;
    catchup.reset(60);
    catchup.configure(0,11988);
    catchup.presented(1000000);
    const auto ordinary=catchup.picture_target(1005000);
    const auto recovery=catchup.picture_target(1005000,true,40000);
    assert(recovery<ordinary && recovery>=1005000);
    moonlight::VrrReadinessReserve readiness;
    readiness.configure(1);
    for(uint32_t i=1;i<70;i++) {
        const uint64_t pt=uint64_t(i)*16666;
        const uint64_t ready=pt+(i%3==0?3500:0);
        readiness.observe(pt,ready,i);
    }
    assert(readiness.reserve_us()<=8000);
    readiness.configure(0);
    assert(readiness.reserve_us()==0);
    moonlight::VrrPreparationLead lead;
    assert(lead.lead_us()==1000);
    lead.observe(3600);
    assert(lead.lead_us()==4100);
    for(int i=0;i<32;i++) lead.observe(400);
    assert(lead.lead_us()==1000);
    // VRR: capture/arrival jitter around normal 60/120 FPS must not
    // schedule a duplicate ahead of the next real picture.
    for(unsigned rate: {60u,120u}) {
        moonlight::VrrRepeatPolicy v;
        v.reset(rate);
        for(uint64_t f=1;f<1000;f++) {
            const uint64_t pts=f*1000000/rate;
            const uint64_t submitted=1000000+pts+(f%2?1000:0);
            v.picture(pts,submitted);
            const uint64_t next=1000000+(f+1)*1000000/rate+((f+1)%2?1000:0);
            assert(v.idle_deadline()>next);
            assert(!v.compensating());
        }
    }
    // Sparse 16 FPS enters integral 5x compensation, then recovers to
    // normal 60 FPS after a source-cadence window. No catch-up bursts.
    moonlight::VrrRepeatPolicy adaptive;
    adaptive.reset(60);
    uint64_t pts=0;
    for(int f=0;f<25;f++) {
        pts+=62500;
        adaptive.picture(pts,1000000+pts);
    }
    assert(adaptive.compensating());
    assert(adaptive.interval()>=8342 && adaptive.interval()<=16000);
    assert(adaptive.source_rate()==16 && adaptive.repeat_factor()==5);
    assert(adaptive.target_refresh_x100()==8000);
    const auto first_repeat=adaptive.deadline();
    adaptive.repeated(first_repeat+200);
    assert(adaptive.deadline()==first_repeat+adaptive.interval());
    adaptive.repeated(first_repeat+1000000);
    assert(adaptive.deadline()>first_repeat+1000000);
    for(int f=0;f<24;f++) {
        pts+=16667;
        adaptive.picture(pts,1000000+pts);
    }
    assert(!adaptive.compensating());
    assert(adaptive.interval()==16000);
    // The fitted repeat factor cannot depend on the starting rate.
    moonlight::VrrRepeatPolicy boundary;
    boundary.reset(60);
    for(int f=1;f<70;f++) boundary.picture(uint64_t(f)*20500,1000000+uint64_t(f)*20500);
    assert(boundary.compensating());
    boundary.reset(30);
    for(int f=1;f<70;f++) boundary.picture(uint64_t(f)*20500,1000000+uint64_t(f)*20500);
    assert(boundary.compensating());
    // One scanout grid covers BOTH source pictures and idle repeats. A
    // jittered source arriving just after a repeat must replace a future slot,
    // never create the 8 ms / 16 ms alternating intervals seen on the console.
    moonlight::VrrRepeatPolicy grid;
    grid.reset(16);
    uint64_t now=1000000, count=1, last_scanout=now;
    grid.picture(62500,now);
    grid.scanned(count,now);
    for(uint64_t frame=2;frame<200;frame++) {
        const uint64_t ready=1000000+(frame-1)*62500+(frame%2?1500:0);
        while(grid.deadline()<ready) {
            const auto repeat=grid.deadline();
            assert(repeat-last_scanout>=grid.interval()*95/100);
            grid.repeated(repeat+200);
            last_scanout=repeat+500;
            grid.scanned(++count,last_scanout);
        }
        grid.observe_picture(frame*62500);
        const auto target=grid.picture_target(ready);
        assert(target>=last_scanout+grid.interval()*95/100-500);
        assert(target-ready<=grid.interval()*2);
        grid.presented(target);
        last_scanout=target+500;
        grid.scanned(++count,last_scanout);
    }
    const auto due=grid.deadline();
    assert(grid.picture_target(due+200)==due+200);
    // A late poll does not pretend scanout just started or restart a slot.
    grid.scanned(++count,last_scanout+1000000);
    assert(grid.deadline()==due);
    assert(grid.picture_target(last_scanout+1000000)==last_scanout+1000000);
    // Source resumes at 60/90/120: exit low-rate compensation after exactly
    // three stable intervals, without a mixed old/static window re-entering it.
    for(unsigned rate: {60u,90u,120u}) {
        moonlight::VrrRepeatPolicy resume;
        resume.reset(rate);
        for(unsigned f=1;f<=16;f++) resume.observe_picture(uint64_t(f)*62500);
        assert(resume.compensating());
        uint64_t pts=17*62500;
        resume.picture(pts,1000000+pts);
        for(int f=1;f<=3;f++) {
            pts+=1000000/rate;
            resume.observe_picture(pts);
            assert(resume.compensating()==(f<3));
        }
        assert(resume.source_rate()==rate);
        for(int f=0;f<20;f++) {
            pts+=1000000/rate;
            resume.observe_picture(pts);
            assert(!resume.compensating());
        }
    }
    // Repeated mixed segments must not confirm a fictitious intermediate
    // moving rate. A homogeneous return to 120 Hz preserves the nominal rate.
    moonlight::VrrRepeatPolicy mixed;
    mixed.reset(120);
    uint64_t mixed_pts=100000;
    mixed.observe_picture(mixed_pts);
    for(int n=0;n<30;n++) {
        for(int f=0;f<12;f++) {
            mixed_pts+=62500; mixed.observe_picture(mixed_pts);
        }
        for(int f=0;f<20;f++) {
            mixed_pts+=8333; mixed.observe_picture(mixed_pts);
        }
        assert(!mixed.compensating() && mixed.source_rate()==120);
    }
    // A new frame is governed by the SAME physical admission floor as
    // repeats, without an additional source-pacer deadline after recovery.
    mixed.presented(1000000);
    mixed.scanned(1,1000000);
    auto moving_target=mixed.picture_target(1000100);
    assert(moving_target==1000000+mixed.display_floor_us());
    mixed.presented(moving_target);
    // Real stable rate changes still fit after a homogeneous source window.
    for(int f=0;f<30;f++) {
        mixed_pts+=16667; mixed.observe_picture(mixed_pts);
    }
    assert(mixed.source_rate()==60);
    mixed.presented(2000000);
    mixed.scanned(2,2000000);
    assert(mixed.picture_target(2000000)>=2016300);
    // Delayed completion polling must not lower moving 120 Hz to ~103 Hz.
    moonlight::VrrRepeatPolicy polled;
    polled.reset(120);
    uint64_t submitted=1000000;
    polled.presented(submitted);
    for(unsigned f=1;f<=120;f++) {
        const uint64_t completed=submitted+1200;
        polled.scanned(f,completed);
        const auto target=polled.picture_target(completed);
        assert(target==submitted+polled.display_floor_us());
        submitted=target;
        polled.presented(submitted);
    }
    assert(submitted==1000000+120*polled.display_floor_us());
    // Reproduce frame 813: a repeat submitted at zero is detected only
    // ~10 ms later. It has already cleared the display ceiling, so a fresh
    // picture must not wait for the old idle-repeat slot.
    moonlight::VrrRepeatPolicy late_repeat;
    late_repeat.reset(60);
    late_repeat.repeated(1000000);
    late_repeat.scanned(1,1010300);
    assert(late_repeat.picture_target(1010300)==1010300);
    late_repeat.scanned(2,1031000);
    assert(late_repeat.picture_target(1031000)==1031000);
    // Recovery preserves readiness learning and cumulative counters.
    moonlight::FramePacing retained;
    retained.reset(90);
    for(int f=1;f<=10;f++) {
        auto t=retained.target(f,uint64_t(f)*11111,1000000+uint64_t(f)*11111);
        retained.submitted(t,t,0);
    }
    const auto reserve=retained.stats.reserve_us;
    retained.resume(2000000);
    auto resumed=retained.target(11,122221,2000000);
    assert(retained.stats.submissions==10 && retained.stats.reserve_us==reserve);
    assert(resumed==2000000);
    assert(retained.admission_limit_us(62500,17000)>=79500);
    assert(retained.admission_limit_us(UINT64_MAX/2,20000)<=100000);
    // An isolated burst must not disengage compensation.
    moonlight::VrrRepeatPolicy burst;
    burst.reset(16);
    burst.observe_picture(62500);
    burst.observe_picture(73500);
    burst.observe_picture(136000);
    assert(burst.compensating());
    // Fixed 60-on-120 must not rush two frames into adjacent refreshes.
    moonlight::FramePacing fixed;
    fixed.reset(60);
    auto first=fixed.target(1,16666,1000000,11988,991660);
    fixed.submitted(first,first,0);
    auto second=fixed.target(2,33333,1000001,11988,first+250);
    assert(second-first>=16000);
    // Client drops may reduce displayed cadence, never the sender rate estimate.
    for (unsigned rate : {60u, 90u, 120u}) {
        moonlight::VrrRepeatPolicy dropped;
        dropped.reset(rate);
        for (uint32_t frame=1; frame<301; frame+=2)
            dropped.observe_picture(uint64_t(frame)*1000000/rate, frame);
        assert(dropped.source_rate()==rate);
        assert(!dropped.compensating());
    }

    moonlight::VrrRepeatPolicy quantized;
    quantized.reset(90);
    for (uint32_t frame=1; frame<601; frame+=(frame%3==1 ? 1 : 2))
        quantized.observe_picture(uint64_t(frame)*4/3*1000000/120, frame);
    assert(quantized.source_rate()>=88 && quantized.source_rate()<=92);

    // Exhaust every Custom FPS, cold start and recovery from a static host.
    // Both adapters pass original frame numbers to this same policy.
    for (unsigned rate=30; rate<=120; ++rate) {
        unsigned expected = rate < 38 ? 3 : rate < 60 ? 2 : 1;
        for (bool sparse_start : {false,true}) for (bool skipped : {false,true}) {
            moonlight::VrrRepeatPolicy custom;
            custom.reset(rate);
            uint64_t pts=100000;
            uint32_t frame=1;
            custom.observe_picture(pts,frame);
            if (sparse_start)
                for (int n=0;n<32;++n) {
                    pts+=62500; custom.observe_picture(pts,++frame);
                }
            const auto origin=pts;
            const auto origin_frame=frame;
            for (unsigned n=1;n<=rate*2;n+=skipped?2:1) {
                pts=origin+uint64_t(n)*1000000/rate;
                custom.observe_picture(pts,origin_frame+n);
            }
            assert(custom.source_rate()==rate);
            assert(custom.repeat_factor()==expected);
            assert(custom.compensating()==(expected>1));
            if(expected>1) assert(custom.interval()>=8333 && custom.interval()<=20000);
        }
    }
    // Real capture jitter around 49--51 must not toggle the LFC factor.
    for (unsigned rate : {49u,50u,51u,59u,60u,61u}) {
        moonlight::VrrRepeatPolicy jitter;
        jitter.reset(rate);
        uint64_t pts=100000;
        jitter.observe_picture(pts,1);
        for (uint32_t frame=2;frame<800;++frame) {
            // Alternating sustained +3%/-3% windows crossed the old threshold.
            pts += (1000000/rate) * (frame/16%2 ? 103 : 97)/100;
            jitter.observe_picture(pts,frame);
            assert(jitter.repeat_factor()==(rate<60?2u:1u));
        }
    }
    // Console traces: three short source intervals made 51 look like 56/60.
    // A negotiated maximum must not be exceeded by fitting or fast recovery.
    for (unsigned rate : {49u,50u,51u,59u,60u,61u,90u,120u}) {
        moonlight::VrrRepeatPolicy spikes;
        spikes.reset(rate);
        uint64_t pts=100000;
        uint32_t frame=1;
        spikes.observe_picture(pts,frame++);
        for (unsigned n=0;n<600;++n) {
            pts += n%40<4 ? 1000000/rate*85/100 : 1000000/rate;
            spikes.observe_picture(pts,frame++);
            assert(spikes.source_rate()==rate);
            assert(spikes.repeat_factor()==(rate<60?2u:1u));
        }
        // Real sparse capture must still lower the estimated source rate.
        for (int n=0;n<40;++n) {pts+=62500;spikes.observe_picture(pts,frame++);}
        assert(spikes.source_rate()==16);
        assert(spikes.repeat_factor()==5);
        assert(spikes.target_refresh_x100()==8000);
        for (int n=0;n<3;++n) {pts+=1000000/rate;spikes.observe_picture(pts,frame++);}
        assert(spikes.source_rate()==rate);
        assert(spikes.repeat_factor()==(rate<60?2u:1u));
    }
    // PS5 60 FPS game trace: short ~55 FPS capture windows incorrectly
    // enabled 110 Hz duplication before immediately recovering to 60.
    for (unsigned profile=0; profile<3; ++profile) {
        moonlight::VrrRepeatPolicy boundary;
        boundary.reset(60); boundary.configure(profile);
        uint64_t pts=100000; uint32_t frame=1;
        boundary.observe_picture(pts,frame++);
        for (unsigned cycle=0;cycle<12;++cycle) {
            for (unsigned n=0;n<13;++n) {
                pts+=18100; boundary.observe_picture(pts,frame++);
                assert(boundary.repeat_factor()==1);
            }
            for (unsigned n=0;n<60;++n) {
                pts+=16666; boundary.observe_picture(pts,frame++);
                assert(boundary.repeat_factor()==1);
            }
        }
        // Sustained lower motion rate still adapts, rather than locking 60 FPS.
        for (unsigned n=0;n<90;++n) {pts+=18100;boundary.observe_picture(pts,frame++);}
        assert(boundary.repeat_factor()==2);
        // A truly sparse desktop does not wait for near-motion confirmation.
        for (unsigned n=0;n<24;++n) {pts+=62500;boundary.observe_picture(pts,frame++);}
        assert(boundary.source_rate()==16);
    }
    // Real 60 FPS trace: a late frame after an idle duplicate waited
    // another 16 ms despite the 119.88 Hz display allowing it after 8.342 ms.
    for(unsigned profile=0;profile<3;++profile) {
        moonlight::VrrRepeatPolicy collision;
        collision.reset(60); collision.configure(profile);
        collision.observe_picture(100000,1);
        collision.presented(1000000);
        const uint64_t duplicate=collision.idle_deadline();
        collision.repeated(duplicate);
        collision.observe_picture(116666,2);
        const uint64_t target=collision.picture_target(duplicate+1300);
        assert(target==duplicate+collision.display_floor_us());
        collision.presented(target);
        assert(!collision.grid_active());
    }
    // All requested rates adapt to sparse capture and recover to their
    // negotiated cadence; low requested FPS must not pin the HDMI grid.
    for (unsigned rate : {30u,45u,49u,50u,51u,59u,60u,90u,120u}) {
        moonlight::VrrRepeatPolicy adaptive_grid;
        adaptive_grid.reset(rate);
        uint64_t pts=100000;uint32_t frame=1;
        adaptive_grid.observe_picture(pts,frame++);
        for(int cycle=0;cycle<4;++cycle) {
            for(int n=0;n<40;++n) {pts+=62500;adaptive_grid.observe_picture(pts,frame++);}
            assert(adaptive_grid.interval()==12500);
            assert(adaptive_grid.target_refresh_x100()==8000);
            for(int n=0;n<80;++n) {pts+=1000000/rate;adaptive_grid.observe_picture(pts,frame++);}
            assert(adaptive_grid.source_rate()==rate);
            assert(adaptive_grid.repeat_factor()==(rate<38?3u:rate<60?2u:1u));
        }
    }
    // Low-rate motion also recovers from a sparse desktop in three intervals.
    for (unsigned rate : {30u,45u,49u,50u,51u,59u}) {
        moonlight::VrrRepeatPolicy recovery;
        recovery.reset(rate);
        uint64_t pts=100000;
        uint32_t frame=1;
        for (int n=0;n<32;++n) { pts+=62500; recovery.observe_picture(pts,frame++); }
        for (int n=0;n<3;++n) { pts+=1000000/rate; recovery.observe_picture(pts,frame++); }
        assert(recovery.source_rate()==rate);
        assert(recovery.repeat_factor()==(rate<38?3u:2u));
    }
    // Low-rate motion uses integral duplication, with headroom before each
    // fresh picture; there is no 20 ms single-scanout watchdog race at 50/51.
    for (unsigned rate : {49u,50u,51u}) {
        moonlight::VrrRepeatPolicy low_motion;
        low_motion.reset(rate);
        for (unsigned frame=1;frame<=600;++frame) {
            const uint64_t pts=uint64_t(frame)*1000000/rate;
            const uint64_t submitted=1000000+pts+(frame%2 ? 1000 : 0);
            low_motion.picture(pts,submitted);
            assert(low_motion.repeat_factor()==2);
            assert(low_motion.interval()<11000);
            assert(low_motion.target_refresh_x100()==rate*200 ||
                   low_motion.target_refresh_x100()==rate*200+1);
        }
    }
    // Nominal 50 FPS stays in 2x repetition despite timestamp rounding.
    moonlight::VrrRepeatPolicy tolerance;
    tolerance.reset(50);
    for (uint32_t frame=1;frame<100;++frame)
        tolerance.observe_picture(uint64_t(frame)*20040,frame);
    assert(tolerance.repeat_factor()==2);

    // Fixed 90 on 120 must alternate 1/1/2 refresh intervals rather than
    // rounding every source frame to two refreshes (the 60 FPS regression).
    moonlight::FramePacing fractional;
    fractional.reset(90);
    uint64_t flip=1000000;
    const uint64_t display=100000000/11988;
    unsigned one=0,two=0;
    for(int f=1;f<=300;f++) {
        const uint64_t ready=flip+300;
        const auto target=fractional.target(f,uint64_t(f)*1000000/90,ready,11988,flip);
        const auto ticks=(target-flip+display-1)/display;
        assert(ticks==1 || ticks==2);
        if(ticks==1) ++one; else ++two;
        fractional.submitted(target,target,target-ready);
        flip+=ticks*display;
    }
    assert(one>=199 && one<=201 && two>=99 && two<=101);
    moonlight::FramePacing p;
    p.reset(120);
    uint64_t last=0;
    for(int f=1;f<400;f++) {
        uint64_t source=f<150?uint64_t(f)*8333:149*8333+uint64_t(f-149)*16666;
        auto target=p.target(f,source,1000000+source);
        p.submitted(target,target,0);
        assert(!last || target>last);
        last=target;
    }
    assert(p.stats.period_us>16000 && p.stats.period_us<17000);
    // 120fps on a matching 119.88Hz display, ready just after the flip.
    // VSync already waits for the next vblank. A deadline near that vblank
    // misses it and the picture stays at half rate.
    moonlight::FramePacing vsync;
    vsync.reset(120);
    const uint64_t anchor=5000000;
    const uint64_t display_period=UINT64_C(100000000)/11988;
    const uint64_t ready=anchor+200;
    const auto early=vsync.target(1,8333,ready,11988,anchor,0,1000);
    assert(early>=ready);
    assert(early<anchor+display_period/2);
    assert(std::strcmp(moonlight::effective_pacing_name(2, false), "Paced")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(2, true), "Paced+VRR")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(1, false), "Paced")==0);
    assert(std::strcmp(moonlight::effective_pacing_name(0, true), "Unpaced")==0);
}
''')
            binary = pathlib.Path(directory) / 'pacing'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
