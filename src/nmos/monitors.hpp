// What each tile's AMWA BCP-008-01 receiver monitor reports, from the tile's
// counts. libmxl-monitoring judges the statuses and counters from two
// snapshots of a tile; what is decided here is which two.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <mxl-monitoring/bcp008.h>
#include <mxl-monitoring/monitoring.h>

namespace nmos_monitors
{
    // A monitor's view of its tile's counts: those of the last poll, and
    // those of the monitor's last counter reset. The poll writes them and the
    // IS-12 callbacks read them, on different threads.
    struct Tile
    {
        std::mutex mu;
        std::optional<mxlMonitoringFlowSnapshot> latest;
        std::optional<mxlMonitoringFlowSnapshot> baseline;
    };

    // Every tile's counts, in the order the tiles were added.
    inline std::vector<mxlMonitoringFlowSnapshot> snapshots(mxlMonitoring monitoring)
    {
        std::vector<mxlMonitoringFlowSnapshot> out;
        std::size_t count = 0;
        do
        {
            out.resize(count);
            count = out.size();
            mxlMonitoringSnapshot(monitoring, out.data(), &count);
        } while (count > out.size());
        out.resize(count);
        return out;
    }

    // Whether a monitor is given statuses from current: not while its tile
    // shows nothing, since nmos-cpp has the monitor inactive then, and not
    // while the tile is still moving to the flow its slot names, since the
    // counts are still the flow it leaves.
    inline bool judged(mxlMonitoringFlowSnapshot const& current, std::string const& shown)
    {
        return current.target[0] != '\0' && shown == current.target;
    }

    // The link, connection, synchronization and stream statuses, judged on
    // what was counted since previous. previous counts another flow where the
    // tile moved between the polls, which leaves current alone to judge on.
    inline std::array<mxlMonitoringBcp008Domain, 4> judge(std::optional<mxlMonitoringFlowSnapshot> const& previous,
        mxlMonitoringFlowSnapshot const& current)
    {
        mxlMonitoringBcp008Statuses statuses{};
        statuses.version = 1;
        statuses.size = sizeof(statuses);
        auto const* const before = previous && std::string{previous->target} == current.target ? &*previous : nullptr;
        if (mxlMonitoringBcp008GetStatuses(before, &current, &statuses) != MXL_STATUS_OK)
        {
            mxlMonitoringBcp008Domain const unchanged{MXL_MONITORING_BCP008_UNCHANGED, {}};
            return {unchanged, unchanged, unchanged, unchanged};
        }
        return {statuses.link, statuses.connection, statuses.synchronization, statuses.stream};
    }

    // A monitor's counters of one kind since baseline, its last reset, or
    // since start without one. A tile keeps its counts whichever flow it
    // shows, so a reset outlives a reconnection.
    inline std::vector<mxlMonitoringBcp008Counter> counters(std::optional<mxlMonitoringFlowSnapshot> const& baseline,
        mxlMonitoringFlowSnapshot const& latest, mxlMonitoringBcp008Counters which)
    {
        std::vector<mxlMonitoringBcp008Counter> out(4);
        std::size_t count = out.size();
        if (mxlMonitoringBcp008GetCounters(baseline ? &*baseline : nullptr, &latest, which, out.data(), &count) != MXL_STATUS_OK)
            return {};
        out.resize(std::min(count, out.size()));
        return out;
    }
}
