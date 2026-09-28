// Unit tests for the order a tile reads its flow's grains in. Built without
// libmxl: the head each tick sees is scripted.

#include "next_grain.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool ok, char const* what)
    {
        if (!ok)
        {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }

    // The grains shown when each tick sees the given head, as the worker
    // reads them. A grain past the head is one the writer has not committed
    // within the tick, so the tick holds.
    std::vector<std::int64_t> shown(std::vector<std::int64_t> const& heads)
    {
        std::vector<std::int64_t> out;
        std::int64_t last = -1;
        for (auto head : heads)
        {
            auto want = next_grain::after(last, head, 2);
            if (want < 0) want = head > last ? head : -1;
            else if (want > head) want = -1;
            if (want >= 0) out.push_back(last = want);
        }
        return out;
    }

    bool contiguous(std::vector<std::int64_t> const& v, std::int64_t first, std::int64_t lastIdx)
    {
        if (v.size() != static_cast<std::size_t>(lastIdx - first + 1)) return false;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i] != first + static_cast<std::int64_t>(i)) return false;
        return true;
    }
}

int main()
{
    check(next_grain::after(-1, 100, 2) == -1, "first read takes the freshest");
    check(next_grain::after(99, 100, 2) == 100, "the next grain when it is the head");
    check(next_grain::after(100, 100, 2) == 101, "the grain after the head is waited for");
    check(next_grain::after(97, 100, 2) == 98, "within the lag: read in order");
    check(next_grain::after(96, 100, 2) == -1, "further behind: catch up");
    check(next_grain::after(500, 100, 2) == -1, "a restarted flow: freshest");

    // A mirror landing grains in pairs: every grain is shown, where sampling
    // the head shows every other one.
    check(contiguous(shown({10, 10, 12, 12, 14, 14, 16, 16}), 10, 16), "grains landing in pairs");

    // The tick drifting across the writer's commit: two ticks see one head,
    // then the head moves by two. No grain is skipped.
    check(contiguous(shown({10, 11, 11, 13, 14, 14, 16}), 10, 15), "tick in phase with the commit");

    // Too far behind: jump to the head rather than replay the backlog.
    auto const behind = shown({10, 20, 21});
    check(behind.size() == 3 && behind[1] == 20 && behind[2] == 21, "catch up from far behind");

    // A tile paces at its flow's rate, and at the mosaic's while none is set.
    check(next_grain::period(50, 1).count() == 20'000'000, "50 fps paces at 20 ms");
    check(next_grain::period(30000, 1001).count() == 33'366'666, "29.97 fps paces at 33.37 ms");
    check(next_grain::period(0, 0).count() == 33'366'700, "an unset rate falls back to 29.97");

    if (failures == 0) std::puts("next_grain_test: ok");
    return failures == 0 ? 0 : 1;
}
