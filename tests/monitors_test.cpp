// Unit tests for which counts a tile's BCP-008-01 monitor is judged on. The
// counts are recorded by hand on a libmxl-monitoring context; no MXL domain
// is opened.

#include "nmos/monitors.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
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

    constexpr char const* kFlowA = "11111111-2222-4333-8444-555555555555";
    constexpr char const* kFlowB = "66666666-7777-4888-9999-000000000000";

    // The tile's flow open and read, as the worker records it.
    void deliver(mxlMonitoringFlow tile, std::uint64_t from, std::uint64_t grains)
    {
        mxlMonitoringRecordAccess(tile, MXL_MONITORING_ACCESS_OPEN, MXL_STATUS_OK);
        for (std::uint64_t i = 0; i < grains; ++i) mxlMonitoringRecordRead(tile, MXL_STATUS_OK, from + i, 1);
    }

    mxlMonitoringFlowSnapshot snapshot(mxlMonitoring monitoring)
    {
        return nmos_monitors::snapshots(monitoring).at(0);
    }

    std::uint64_t counter(std::vector<mxlMonitoringBcp008Counter> const& counters, char const* name)
    {
        for (auto const& c : counters)
            if (std::strcmp(c.name, name) == 0) return c.value;
        return ~0ULL;
    }
}

int main()
{
    mxlMonitoring monitoring{};
    mxlMonitoringFlow tile{};
    if (mxlMonitoringCreate(nullptr, &monitoring) != MXL_STATUS_OK ||
        mxlMonitoringAddFlow(monitoring, "tile-0", MXL_MONITORING_ROLE_READER, &tile) != MXL_STATUS_OK)
    {
        std::fprintf(stderr, "FAIL: cannot set up monitoring\n");
        return 1;
    }

    // A tile with nothing connected is left to nmos-cpp, which has its
    // monitor inactive; one still on the flow its slot moved away from
    // would report that flow's health under the new connection.
    mxlMonitoringSetFlowTarget(tile, "");
    check(!nmos_monitors::judged(snapshot(monitoring), ""), "a disconnected tile is not judged");
    mxlMonitoringSetFlowTarget(tile, kFlowA);
    check(!nmos_monitors::judged(snapshot(monitoring), kFlowB), "a tile still moving is not judged");
    check(nmos_monitors::judged(snapshot(monitoring), kFlowA), "a tile on its slot's flow is judged");

    // A flow read without a loss is a healthy connection; grains skipped
    // since the poll before make it unhealthy.
    deliver(tile, 100, 10);
    auto const first = snapshot(monitoring);
    deliver(tile, 110, 10);
    auto const healthy = nmos_monitors::judge(first, snapshot(monitoring));
    check(healthy[1].status == MXL_MONITORING_BCP008_HEALTHY, "a flow read in order is a healthy connection");
    check(healthy[0].status == MXL_MONITORING_BCP008_HEALTHY, "the domain is up");
    auto const before = snapshot(monitoring);
    mxlMonitoringRecordSkipped(tile, 3);
    deliver(tile, 123, 10);
    auto const lossy = nmos_monitors::judge(before, snapshot(monitoring));
    check(lossy[1].status == MXL_MONITORING_BCP008_UNHEALTHY, "skipped grains are an unhealthy connection");

    // A tile moved to another flow between two polls: what was lost on the
    // flow it left is not held against the one it shows now.
    auto const onA = snapshot(monitoring);
    mxlMonitoringRecordSkipped(tile, 5);
    mxlMonitoringSetFlowTarget(tile, kFlowB);
    deliver(tile, 500, 10);
    auto const moved = nmos_monitors::judge(onA, snapshot(monitoring));
    check(moved[1].status == MXL_MONITORING_BCP008_HEALTHY, "a loss on the flow left is not the new flow's");

    // Lost counters count since the monitor's last reset, and since start
    // without one. 8 grains were skipped in all, 5 of them since onA.
    auto const latest = snapshot(monitoring);
    check(counter(nmos_monitors::counters(std::nullopt, latest, MXL_MONITORING_BCP008_LOST), "skipped") == 8,
        "lost counts since start");
    check(counter(nmos_monitors::counters(onA, latest, MXL_MONITORING_BCP008_LOST), "skipped") == 5,
        "lost counts since the last reset");

    mxlMonitoringDestroy(monitoring);
    if (failures == 0) std::puts("monitors_test: ok");
    return failures == 0 ? 0 : 1;
}
