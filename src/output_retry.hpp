// When the RTSP output is tried again after it failed. The receiving server
// may not be up yet, or restart under a running mosaic; the readers keep
// reading meanwhile, and only the output waits.

#pragma once

#include <algorithm>
#include <chrono>

namespace output_retry
{
    class Backoff
    {
    public:
        using duration = std::chrono::milliseconds;

        Backoff(duration first, duration most, duration stable)
            : first_{first}, most_{most}, stable_{stable}
        {
        }

        // The wait before the next try, for an output that ran for ranFor
        // before it failed: doubled from first on each failure in a row, at
        // most most. One that ran stable for that long failed afresh and
        // starts again from first.
        duration next(duration ranFor)
        {
            if (ranFor >= stable_) failures_ = 0;
            auto wait = first_;
            for (int i = 0; i < failures_ && wait < most_; ++i) wait *= 2;
            ++failures_;
            return std::min(wait, most_);
        }

    private:
        duration first_;
        duration most_;
        duration stable_;
        int failures_{0};
    };
}
