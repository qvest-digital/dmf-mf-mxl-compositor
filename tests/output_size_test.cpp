// Unit tests for reading MXL_OUTPUT_WIDTH and MXL_OUTPUT_HEIGHT.

#include "output_size.hpp"

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

    bool is(std::optional<output_size::Size> s, int w, int h)
    {
        return s && s->width == w && s->height == h;
    }

    constexpr output_size::Size kCanvas{3840, 2160};
}

int main()
{
    // Unset is the default: the mosaic is encoded at its canvas size, as it
    // was before the option existed.
    check(is(output_size::resolve("", "", kCanvas), 3840, 2160), "unset keeps the canvas");
    check(is(output_size::resolve("1920", "1080", kCanvas), 1920, 1080), "a size is read");
    // Upscaling and another aspect are the operator's choice, not refused.
    check(is(output_size::resolve("7680", "4320", kCanvas), 7680, 4320), "a larger size is read");
    check(is(output_size::resolve("1080", "1920", kCanvas), 1080, 1920), "another aspect is read");

    // Half a size, or one that is not, fails the start instead of encoding at
    // a size nobody chose. Odd dimensions cannot carry 4:2:0 chroma.
    struct Bad
    {
        char const* w;
        char const* h;
    };
    for (auto const& bad : {Bad{"1920", ""}, Bad{"", "1080"}, Bad{"0", "1080"}, Bad{"1920", "-1080"},
             Bad{"1921", "1080"}, Bad{"1920", "1081"}, Bad{"1920.0", "1080"}, Bad{" 1920", "1080"},
             Bad{"wide", "1080"}, Bad{"999999", "1080"}})
    {
        check(!output_size::resolve(bad.w, bad.h, kCanvas), bad.w[0] ? bad.w : bad.h);
    }

    if (failures == 0) std::puts("output_size_test: ok");
    return failures == 0 ? 0 : 1;
}
