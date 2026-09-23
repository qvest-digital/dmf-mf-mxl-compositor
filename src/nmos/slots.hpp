// Per-tile flow selection shared between the NMOS activation callback and the
// reader workers.
//
// A tile shows whatever flow its slot names. The callback writes a slot, a
// worker reads it once per grain period and reopens its reader when the name
// changes. An empty name is a tile with nothing connected, drawn black.

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace nmos_slots
{
    class Slots
    {
    public:
        explicit Slots(std::vector<std::string> initial)
            : flows_{std::move(initial)}
        {}

        std::size_t size() const { return flows_.size(); }

        std::string get(std::size_t i) const
        {
            std::lock_guard<std::mutex> lock{mu_};
            return i < flows_.size() ? flows_[i] : std::string{};
        }

        // set reports whether the slot exists. Setting the name it already
        // holds is not a change, so a controller re-sending an activation does
        // not make the worker drop a live reader.
        bool set(std::size_t i, std::string flow)
        {
            std::lock_guard<std::mutex> lock{mu_};
            if (i >= flows_.size()) return false;
            flows_[i] = std::move(flow);
            return true;
        }

    private:
        mutable std::mutex mu_;
        std::vector<std::string> flows_;
    };
}
