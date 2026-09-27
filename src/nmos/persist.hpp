// Keeps each tile's IS-05 connection in a store that outlives the process.
//
// The activation callback only marks a tile; a thread of its own reads the
// Receiver's /active and writes what changed, because the library holds its
// model lock while it calls back and a store may be slow or unreachable.
// Reading /active rather than taking the callback's flow definition is what
// captures sender_id, which the library does not hand the application.
//
// Each write touches only the tiles that changed, so a store this process
// could not read at startup is never overwritten wholesale.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "nmos/connections.hpp"

namespace nmos_persist
{
    struct Io
    {
        // The Receiver's /active body for a tile, or nothing where it could
        // not be read.
        std::function<std::optional<std::string>(std::size_t tile)> readActive;
        // Applies a merge patch to the store; false with the reason in error.
        std::function<bool(std::string const& patch, std::string& error)> write;
    };

    class Persister
    {
    public:
        // keys names the store entry of each tile. known is what the store
        // holds for each tile it was read for, a connection or nothing, so a
        // tile that did not change is not rewritten. A tile left out of it,
        // because the store could not be read or held something unreadable
        // for it, has its first state written whatever that is, a clear
        // included.
        Persister(Io io, std::vector<std::string> keys,
            std::map<std::size_t, std::optional<nmos_connections::Connection>> known);
        ~Persister();

        // mark queues a tile for a write. Ignored once stop has begun.
        void mark(std::size_t tile);

        // sync writes every marked tile, and reports whether nothing is left
        // pending. A tile whose /active could not be read or whose write failed
        // stays marked.
        bool sync();

        // start runs sync on a thread of its own, on every mark and, while
        // anything is pending, again after a back-off.
        void start();

        // stop makes one last attempt at what is pending, then ends the thread.
        void stop();

    private:
        void run();

        Io io_;
        std::vector<std::string> keys_;
        std::map<std::size_t, std::optional<nmos_connections::Connection>> known_;
        std::mutex mu_;
        std::condition_variable cv_;
        std::set<std::size_t> pending_;
        bool stopping_{false};
        std::thread thread_;
        int failures_{0};
    };
}
