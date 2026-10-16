/* ps5-native-app-boilerplate - Clock-injected native waveform playout policy.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_waveform.hpp"
#include <cstring>
namespace prosperolight::waveform
{
void Buffer::Discard()
{
    head = count = nonzero_frames = 0;
}
void Buffer::Clear()
{
    Discard();
    sequenced = false;
}
bool Buffer::Push(uint32_t next, const uint8_t *pcm, unsigned frames, uint64_t received_us,
                  bool store)
{
    if (!pcm || !frames || frames > PacketFrames)
    {
        ++dropped;
        return false;
    }
    if (sequenced)
    {
        const uint32_t delta = next - sequence;
        if (delta == 0)
        {
            ++duplicate;
            return false;
        }
        if (delta >= 0x80000000u)
        {
            ++out_of_order;
            return false;
        }
        if (delta != 1)
        {
            ++gaps;
            Discard();
        }
    }
    sequenced = true;
    sequence = next;
    ++received;
    input_frames += frames;
    if (!store)
    {
        ignored_silent_frames += frames;
        return true;
    }
    if (count + frames > Capacity)
    {
        ++dropped;
        ++overflow;
        Discard();
    }
    for (unsigned f = 0; f < frames; ++f)
    {
        const unsigned index = (head + count + f) % Capacity;
        arrival[index] = received_us;
        for (unsigned c = 0; c < Channels; ++c)
        {
            const unsigned p = (f * Channels + c) * 2;
            const uint16_t bits = uint16_t(pcm[p]) | (uint16_t(pcm[p + 1]) << 8);
            // Preserve every signed sample bit, including -32768.
            std::memcpy(&samples[index * Channels + c], &bits, 2);
        }
        if (samples[index * Channels] || samples[index * Channels + 1])
            ++nonzero_frames;
    }
    count += frames;
    return true;
}
unsigned Buffer::Pop(int16_t *output, RenderMetadata *metadata)
{
    const unsigned n = count < Grain ? count : Grain;
    if (metadata)
    {
        metadata->frames = n;
        metadata->queue_frames = count;
    }
    for (unsigned f = 0; f < n; ++f)
    {
        const unsigned index = (head + f) % Capacity;
        if (metadata)
            metadata->arrival[f] = arrival[index];
        if (samples[index * Channels] || samples[index * Channels + 1])
            --nonzero_frames;
        for (unsigned c = 0; c < Channels; ++c)
            output[f * Channels + c] = samples[index * Channels + c];
    }
    std::memset(output + n * Channels, 0, (Grain - n) * Channels * sizeof(int16_t));
    head = (head + n) % Capacity;
    count -= n;
    dequeued_frames += n;
    if (n < Grain)
    {
        ++underruns;
        padded_frames += Grain - n;
    }
    return n;
}

void Distribution::Observe(uint64_t value)
{
    const uint64_t bucket = value / quantum + (value % quantum != 0);
    ++buckets[bucket < Bins ? bucket : Bins - 1];
    ++samples;
    if (value > maximum)
        maximum = value;
}
DistributionSummary Distribution::Summary() const
{
    DistributionSummary result{};
    result.samples = samples;
    result.maximum = maximum;
    if (!samples)
        return result;
    const unsigned percentages[] = {50, 95, 99};
    uint64_t *values[] = {&result.p50, &result.p95, &result.p99};
    for (unsigned p = 0; p < 3; ++p)
    {
        const uint64_t rank =
            (samples / 100) * percentages[p] + ((samples % 100) * percentages[p] + 99) / 100;
        uint64_t total = 0;
        for (unsigned b = 0; b < Bins; ++b)
        {
            total += buckets[b];
            if (total >= rank)
            {
                *values[p] = b == Bins - 1 ? maximum : uint64_t(b) * quantum;
                break;
            }
        }
    }
    return result;
}
void Playout::Reset(bool reset_sequence)
{
    if (reset_sequence)
        buffer.Clear();
    else
        buffer.Discard();
    if (reset_sequence)
        last_packet = last_nonzero = 0;
    first_packet = first_nonzero = pending_pad = 0;
    effect = primed = first_pending = false;
}
bool Playout::Receive(uint32_t sequence, const uint8_t *pcm, unsigned frames, uint64_t arrival)
{
    if (!pcm || !frames || frames > PacketFrames)
    {
        ++buffer.dropped;
        return false;
    }
    bool nonzero = false;
    for (unsigned b = 0; b < frames * Channels * 2; ++b)
        if (pcm[b])
        {
            nonzero = true;
            break;
        }
    // Always validate/advance input sequence, including ignored idle silence.
    // A delayed nonzero packet must never re-acquire ownership after newer zeros.
    if (!buffer.Push(sequence, pcm, frames, arrival, nonzero || effect))
        return false;
    last_packet = arrival;
    if (nonzero)
    {
        if (!effect)
        {
            first_packet = first_nonzero = arrival;
            first_pending = true;
            effect = true;
        }
        last_nonzero = arrival;
    }
    if (effect)
    {
        starvation_frames += pending_pad;
        pending_pad = 0;
    }
    return true;
}
bool Playout::WantsOutput(uint64_t now)
{
    if (!effect)
        return false;
    if (!buffer.nonzero_frames && now >= last_nonzero && now - last_nonzero >= 60000)
    {
        // Only queued zeros remain. Stop writing them over ordinary rumble,
        // without resetting the sequence tracker or refreshing nonzero activity.
        buffer.ignored_silent_frames += buffer.count;
        Reset(false);
        return false;
    }
    if (!primed && now >= first_packet && policy.Ready(buffer.count, Capacity, now - first_packet))
        primed = true;
    return primed;
}
unsigned Playout::Pop(int16_t *output, RenderMetadata *metadata)
{
    const unsigned n = buffer.Pop(output, metadata);
    pending_pad += Grain - n;
    return n;
}
void Playout::RecordOutput(uint64_t start, uint64_t blocked_us, const int16_t *output,
                           const RenderMetadata &metadata, bool accepted)
{
    blocking.Observe(blocked_us);
    if (!accepted)
        return;
    if (metadata.frames)
        queue_depth.Observe(metadata.queue_frames);
    for (unsigned f = 0; f < metadata.frames; ++f)
    {
        if (start >= metadata.arrival[f])
            receive_to_output.Observe(start - metadata.arrival[f]);
        if (first_pending && (output[f * Channels] || output[f * Channels + 1]))
        {
            if (start >= first_nonzero)
                first_nonzero_delay.Observe(start - first_nonzero);
            first_pending = false;
        }
    }
}
} // namespace prosperolight::waveform
