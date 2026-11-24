// The size the mosaic is encoded at, as MXL_OUTPUT_WIDTH and
// MXL_OUTPUT_HEIGHT write it. Neither set keeps the canvas size.

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace output_size
{
    struct Size
    {
        int width;
        int height;
    };

    // The canvas size where neither is set, the size given where both are, and
    // empty otherwise: one without the other, a value that is not a positive
    // whole number, or an odd one, which 4:2:0 chroma cannot carry. A typo
    // fails the start rather than encoding a size nobody chose.
    inline std::optional<Size> resolve(std::string const& width, std::string const& height, Size canvas)
    {
        if (width.empty() && height.empty()) return canvas;
        auto const whole = [](std::string const& s) -> int {
            if (s.empty() || s.size() > 5 || s.find_first_not_of("0123456789") != std::string::npos) return 0;
            return std::atoi(s.c_str());
        };
        int const w = whole(width);
        int const h = whole(height);
        if (w <= 0 || h <= 0 || w % 2 != 0 || h % 2 != 0) return std::nullopt;
        return Size{w, h};
    }
}
