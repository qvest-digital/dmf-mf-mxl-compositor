#include "nmos/persist.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace nmos_persist
{
    Persister::Persister(Io io, std::vector<std::string> keys,
        std::map<std::size_t, std::optional<nmos_connections::Connection>> known)
        : io_{std::move(io)}, keys_{std::move(keys)}, known_{std::move(known)}
    {
    }

    Persister::~Persister() { stop(); }

    void Persister::mark(std::size_t tile)
    {
        if (tile >= keys_.size()) return;
        {
            std::lock_guard<std::mutex> lock{mu_};
            if (stopping_) return;
            pending_.insert(tile);
        }
        cv_.notify_one();
    }

    bool Persister::sync()
    {
        std::set<std::size_t> tiles;
        {
            std::lock_guard<std::mutex> lock{mu_};
            tiles.swap(pending_);
        }
        std::set<std::size_t> retry;
        std::map<std::size_t, std::optional<nmos_connections::Connection>> changed;
        for (auto tile : tiles)
        {
            auto active = io_.readActive(tile);
            if (!active)
            {
                retry.insert(tile);
                continue;
            }
            auto now = nmos_connections::from_active(*active);
            auto was = known_.find(tile);
            if (was == known_.end() || was->second != now) changed.emplace(tile, now);
        }

        if (!changed.empty())
        {
            std::map<std::string, std::optional<std::string>> values;
            for (auto const& [tile, conn] : changed)
                values.emplace(keys_[tile], conn ? std::optional<std::string>{nmos_connections::to_json(*conn)}
                                                 : std::nullopt);
            std::string error;
            if (io_.write(nmos_connections::data_patch(values), error))
            {
                failures_ = 0;
                for (auto const& [tile, conn] : changed)
                {
                    known_[tile] = conn;
                    std::fprintf(stderr, "nmos: %s connection %s\n", keys_[tile].c_str(),
                        conn ? ("stored as " + conn->flowId).c_str() : "cleared from the store");
                }
            }
            else
            {
                for (auto const& entry : changed) retry.insert(entry.first);
                // One line per run of failures, then one every tenth, so an
                // unreachable store is visible without flooding the log.
                if (failures_++ % 10 == 0)
                    std::fprintf(stderr, "nmos: could not store connections, retrying: %s\n", error.c_str());
            }
        }

        std::lock_guard<std::mutex> lock{mu_};
        pending_.insert(retry.begin(), retry.end());
        return pending_.empty();
    }

    void Persister::start()
    {
        thread_ = std::thread{[this] { run(); }};
    }

    void Persister::run()
    {
        auto backoff = std::chrono::seconds{1};
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock{mu_};
                cv_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
                if (stopping_) break;
            }
            if (sync())
            {
                backoff = std::chrono::seconds{1};
                continue;
            }
            std::unique_lock<std::mutex> lock{mu_};
            cv_.wait_for(lock, backoff, [this] { return stopping_; });
            if (stopping_) break;
            backoff = std::min(backoff * 2, std::chrono::seconds{30});
        }
        sync();
    }

    void Persister::stop()
    {
        {
            std::lock_guard<std::mutex> lock{mu_};
            stopping_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
}
