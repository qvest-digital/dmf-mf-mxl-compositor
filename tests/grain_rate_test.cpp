// Unit tests for reading MXL_GRAIN_RATE.

#include "grain_rate.hpp"

#include <cstdio>

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

    bool is(std::optional<grain_rate::Rate> r, int n, int d)
    {
        return r && r->numerator == n && r->denominator == d;
    }
}

int main()
{
    check(is(grain_rate::parse("30000/1001"), 30000, 1001), "a fraction is read");
    check(is(grain_rate::parse("50"), 50, 1), "a whole number is per second");

    // A rate that is not one fails the start instead of setting the mosaic,
    // its tiles and the NMOS caps to it.
    for (char const* bad : {"", "0", "50/0", "-50/1", "50/", "/1", "50.0", "50/1/1", " 50", "fifty", "9999999999/1"})
    {
        check(!grain_rate::parse(bad), bad);
    }

    if (failures == 0) std::puts("grain_rate_test: ok");
    return failures == 0 ? 0 : 1;
}
