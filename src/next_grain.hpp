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

    // How far behind the head a tile ticking at tickN/tickD may read a flow of
    // flowN/flowD in order. A flow no faster than the tick is read grain by
    // grain within maxLag. A faster one is sampled: the head each tick, so its
    // grains drop evenly instead of the reader falling behind until it jumps.
    // A flow that reports no rate is read in order.
    inline std::int64_t lag_for(std::int64_t flowN, std::int64_t flowD, std::int64_t tickN, std::int64_t tickD,
        std::int64_t maxLag)
    {
        if (flowN <= 0 || flowD <= 0) return maxLag;
        return flowN * tickD > tickN * flowD ? 0 : maxLag;
    }

    // The time between two grains at numerator/denominator grains per second.
    inline std::chrono::nanoseconds period(std::int64_t numerator, std::int64_t denominator)
    {
        return std::chrono::nanoseconds{1'000'000'000LL * denominator / numerator};
    }
}
