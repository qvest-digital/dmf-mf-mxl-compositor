// The compositor's grain rate, as MXL_GRAIN_RATE writes it:
// "numerator/denominator" or a whole number of grains per second.

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace grain_rate
{
    struct Rate
    {
        int numerator;
        int denominator;
    };

    // Empty for anything but two positive integers, so a typo fails the
    // start rather than running the mosaic at a rate nobody chose.
    inline std::optional<Rate> parse(std::string const& text)
    {
        auto const whole = [](std::string const& s) -> int {
            if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return 0;
            return std::atoi(s.c_str());
        };
        auto const slash = text.find('/');
        int const n = whole(text.substr(0, slash));
        int const d = slash == std::string::npos ? 1 : whole(text.substr(slash + 1));
        if (n <= 0 || d <= 0) return std::nullopt;
        return Rate{n, d};
    }
}
