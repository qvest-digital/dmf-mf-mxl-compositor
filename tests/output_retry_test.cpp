// Unit tests for the RTSP output's retry delays.

#include "output_retry.hpp"

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
}

int main()
{
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    output_retry::Backoff b{seconds(1), seconds(30), seconds(60)};

    // A server that is not there yet is tried again ever less often, and
    // never less often than the bound.
    long const want[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (long w : want) check(b.next(milliseconds(0)).count() == w, "doubled up to the bound");

    // An output that ran long enough failed afresh: it is tried again soon.
    check(b.next(seconds(60)).count() == 1000, "reset after a stable run");
    check(b.next(seconds(5)).count() == 2000, "a short run counts as a failure in a row");

    // Many failures in a row stay bounded without overflowing.
    for (int i = 0; i < 1000; ++i) b.next(milliseconds(0));
    check(b.next(milliseconds(0)).count() == 30000, "bounded after many failures");

    if (failures == 0) std::puts("output_retry_test: ok");
    return failures == 0 ? 0 : 1;
}
