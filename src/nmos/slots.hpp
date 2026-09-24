// Per-tile source selection shared by the NMOS activation callback and the
// reader workers.
//
// A tile shows the flow its slot names, in the domain directory it names. The
// callback writes the slot, and the worker reads it once per grain period and
// reopens its reader when it changes. An empty flow is a tile with nothing
// connected, drawn black.

#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace nmos_slots
{
    struct Source
    {
        // The MXL domain directory the flow is read from.
        std::string domain;
        std::string flow;

        bool operator==(Source const& o) const { return domain == o.domain && flow == o.flow; }
        bool operator!=(Source const& o) const { return !(*this == o); }
    };

    class Slots
    {
    public:
        explicit Slots(std::vector<Source> initial) : sources_{std::move(initial)} {}

        std::size_t size() const { return sources_.size(); }

        Source get(std::size_t i) const
        {
            std::lock_guard<std::mutex> lock{mu_};
            return i < sources_.size() ? sources_[i] : Source{};
        }

        // set reports whether the slot exists. Setting the source it already
        // holds does not change it, so a controller re-sending an activation
        // does not make the worker drop a live reader.
        bool set(std::size_t i, Source source)
        {
            std::lock_guard<std::mutex> lock{mu_};
            if (i >= sources_.size()) return false;
            sources_[i] = std::move(source);
            return true;
        }

    private:
        mutable std::mutex mu_;
        std::vector<Source> sources_;
    };
}
