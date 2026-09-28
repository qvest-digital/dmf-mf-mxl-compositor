// Which grain a tile's worker reads next.
//
// A tile reads its flow in order, one grain per grain period: the grain after
// the last one shown, waited for if the writer has not committed it yet.
// Sampling the head instead loses grains whenever the reader's tick and the
// writer's commit are out of phase, or a mirror lands grains in bursts: two
// ticks see the same head, the second holds, and the grain between two heads
// is never shown, though it was readable the whole time.

#pragma once

#include <chrono>
#include <cstdint>

namespace next_grain
{
    // The index to read after lastShown with the flow's head at head, or -1
    // to take the freshest grain instead. That is the case on the first read
    // and after a reopen (lastShown < 0), when the reader has fallen more than
    // maxLag grains behind the head and catches up rather than showing ever
    // older content, and when the head is behind the last shown grain, which
    // a flow restarted under the same id does.
    inline std::int64_t after(std::int64_t lastShown, std::int64_t head, std::int64_t maxLag)
    {
        if (lastShown < 0) return -1;
        std::int64_t const next = lastShown + 1;
        if (next > head + 1 || head - next > maxLag) return -1;
        return next;
    }

    // The time between two grains at numerator/denominator grains per second.
    // A rate that is not set paces at 30000/1001, the mosaic's own rate.
    inline std::chrono::nanoseconds period(std::int64_t numerator, std::int64_t denominator)
    {
        if (numerator <= 0 || denominator <= 0) return std::chrono::nanoseconds{33'366'700LL};
        return std::chrono::nanoseconds{1'000'000'000LL * denominator / numerator};
    }
}
