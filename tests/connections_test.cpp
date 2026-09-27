// Unit tests for keeping a tile's connection across restarts: what is read
// from /active, what is stored, what is replayed, and when the store is
// written. Built without the NMOS library, libmxl or a network.

#include "nmos/connections.hpp"
#include "nmos/persist.hpp"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
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

    std::string const S = "7e2ae7d2-5c7f-4c4f-9f0e-2d7f8f6b1a01";
    std::string const D = "fec11c1b-9fab-4275-ab2c-ef676fa2e081";
    std::string const F = "11111111-2222-4333-8444-555555555555";

    // /active as nmos-cpp serves it for a connected MXL Receiver.
    std::string active(std::string const& sender, bool enable, std::string const& domain,
        std::string const& flow)
    {
        auto q = [](std::string const& s) { return s.empty() ? std::string{"null"} : "\"" + s + "\""; };
        return R"({"activation":{"activation_time":"1790000000:0","mode":"activate_immediate","requested_time":null},)"
               R"("master_enable":)" + std::string{enable ? "true" : "false"} +
               R"(,"sender_id":)" + q(sender) +
               R"(,"transport_file":{"data":null,"type":null},"transport_params":[{"mxl_domain_id":)" + q(domain) +
               R"(,"mxl_flow_id":)" + q(flow) + "}]}";
    }

    struct FakeStore
    {
        std::map<std::size_t, std::string> actives;
        std::vector<std::string> patches;
        bool up{true};

        nmos_persist::Io io()
        {
            nmos_persist::Io io;
            io.readActive = [this](std::size_t i) -> std::optional<std::string> {
                auto it = actives.find(i);
                if (it == actives.end()) return std::nullopt;
                return it->second;
            };
            io.write = [this](std::string const& patch, std::string& error) {
                if (!up)
                {
                    error = "store down";
                    return false;
                }
                patches.push_back(patch);
                return true;
            };
            return io;
        }
    };
}

int main()
{
    using nmos_connections::Connection;

    // The Sender a controller named is what the activation callback does not
    // carry, and what a restart has to bring back.
    {
        auto c = nmos_connections::from_active(active(S, true, D, F));
        check(c && c->senderId == S && c->domainId == D && c->flowId == F, "/active yields sender, domain and flow");
        auto noSender = nmos_connections::from_active(active("", true, "", F));
        check(noSender && noSender->senderId.empty() && noSender->domainId.empty(),
            "a connection made without naming a sender or domain is still one");
        check(!nmos_connections::from_active(active(S, false, D, F)), "master_enable false is no connection");
        check(!nmos_connections::from_active(active(S, true, D, "")), "no flow is no connection");
        check(!nmos_connections::from_active(active("not-a-uuid", true, D, F)), "a sender id that is not a UUID is refused");
        check(!nmos_connections::from_active(R"({"master_enable":true,"transport_params":[{"mxl_flow_id":")" + F + "\""),
            "a truncated body is refused");
        check(!nmos_connections::from_active("garbage"), "garbage is refused");
    }

    // What is stored reads back as the same connection, and replays as an
    // immediate activation with the same fields.
    {
        Connection c{S, D, F};
        auto back = nmos_connections::from_active(nmos_connections::to_json(c));
        check(back && *back == c, "a stored connection reads back unchanged");
        auto patch = nmos_connections::staged_patch(c);
        check(patch.find(R"("activation":{"mode":"activate_immediate"})") != std::string::npos,
            "the replay activates at once");
        auto replay = nmos_connections::from_active(patch);
        check(replay && *replay == c, "the replay carries sender, domain and flow");
        Connection bare{"", "", F};
        check(nmos_connections::to_json(bare).find(R"("sender_id":null)") != std::string::npos &&
                nmos_connections::to_json(bare).find(R"("mxl_domain_id":null)") != std::string::npos,
            "no sender or domain is stored as null, as IS-05 has it");
    }

    // The API server returns each value as a JSON string, escaped; the keys
    // under metadata.managedFields name the same tiles and are not data. A
    // \u escape outside ASCII elsewhere in the document is still JSON.
    {
        Connection c{S, D, F};
        std::string value = nmos_connections::to_json(c);
        std::string escaped;
        for (char ch : value) escaped += ch == '"' ? std::string{"\\\""} : std::string{ch};
        std::string cm = R"({"kind":"ConfigMap","apiVersion":"v1","metadata":{"name":"x",)"
                         R"("annotations":{"note":"a\u2028b"},"managedFields":)"
                         R"([{"fieldsV1":{"f:data":{"f:tile-0":{},"tile-1":"decoy"}}}]},"data":{"tile-0":")" +
                         escaped + R"(","tile-2":"\u0041"}})";
        auto data = nmos_connections::configmap_data(cm);
        check(data && data->size() == 2, "only the keys under data are read");
        check(data && data->count("tile-1") == 0, "a key under metadata is not data");
        auto stored = data && data->count("tile-0") ? nmos_connections::from_active(data->at("tile-0")) : std::nullopt;
        check(stored && *stored == c, "an escaped stored value decodes to the connection");
        check(data && data->at("tile-2") == "A", "a \\u escape decodes");
        auto empty = nmos_connections::configmap_data(R"({"kind":"ConfigMap","metadata":{}})");
        check(empty && empty->empty(), "a ConfigMap with no data has no entries");
        check(!nmos_connections::configmap_data(R"({"kind":"ConfigMap","data":{"tile-0":"\u00)"),
            "a truncated document is not taken for an empty ConfigMap");
        check(!nmos_connections::configmap_data("not json"), "garbage is not taken for an empty ConfigMap");
        check(!nmos_connections::from_active(std::string(100000, '[')), "deep nesting is refused, not recursed into");
    }

    // A merge patch sets what connected and removes what disconnected.
    check(nmos_connections::data_patch({{"tile-0", std::string{R"({"a":"b"})"}}, {"tile-1", std::nullopt}}) ==
            R"({"data":{"tile-0":"{\"a\":\"b\"}","tile-1":null}})",
        "the patch escapes the value and nulls a cleared tile");

    std::vector<std::string> const keys{"tile-0", "tile-1"};

    // A controller's activation is stored, a later one replaces it, and a
    // disconnect clears it.
    {
        FakeStore store;
        nmos_persist::Persister p{store.io(), keys, {{0, std::nullopt}, {1, std::nullopt}}};
        store.actives[0] = active(S, true, D, F);
        p.mark(0);
        auto written = [&](std::size_t n) {
            auto data = n < store.patches.size() ? nmos_connections::configmap_data(store.patches[n]) : std::nullopt;
            return data && data->count("tile-0") ? nmos_connections::from_active(data->at("tile-0")) : std::nullopt;
        };
        check(p.sync() && store.patches.size() == 1 && written(0) && *written(0) == Connection{S, D, F},
            "an activation is written with its sender");
        std::string const other = "22222222-2222-4333-8444-555555555555";
        store.actives[0] = active(S, true, D, other);
        p.mark(0);
        check(p.sync() && store.patches.size() == 2 && store.patches[1].find(other) != std::string::npos,
            "a later activation replaces the stored one");
        store.actives[0] = active(S, false, D, other);
        p.mark(0);
        check(p.sync() && store.patches.size() == 3 && store.patches[2] == R"({"data":{"tile-0":null}})",
            "a disconnect clears the tile");
        p.mark(0);
        check(p.sync() && store.patches.size() == 3, "a tile already clear is not written again");
    }

    // Replaying the stored connection at startup calls back like any
    // activation; what the store holds already is not written again.
    {
        FakeStore store;
        nmos_persist::Persister p{store.io(), keys, {{0, Connection{S, D, F}}, {1, std::nullopt}}};
        store.actives[0] = active(S, true, D, F);
        p.mark(0);
        check(p.sync() && store.patches.empty(), "a restored tile is not rewritten");
    }

    // A store that could not be read at startup, or held something
    // unreadable for a tile, is not trusted to be empty: that tile's first
    // state is written whatever it is.
    {
        FakeStore store;
        nmos_persist::Persister p{store.io(), keys, {}};
        store.actives[1] = active("", false, "", "");
        p.mark(1);
        check(p.sync() && store.patches.size() == 1 && store.patches[0] == R"({"data":{"tile-1":null}})",
            "with the store unread, a clear is written too");
    }

    // An unreachable store or an unreadable /active leaves the tile pending,
    // and the next sync writes the state current then.
    {
        FakeStore store;
        store.up = false;
        nmos_persist::Persister p{store.io(), keys, {{0, std::nullopt}, {1, std::nullopt}}};
        store.actives[0] = active(S, true, D, F);
        p.mark(0);
        p.mark(1);
        check(!p.sync() && store.patches.empty(), "a failed write stays pending");
        store.up = true;
        store.actives[1] = active(S, true, "", F);
        check(p.sync() && store.patches.size() == 1 &&
                store.patches[0].find("tile-0") != std::string::npos &&
                store.patches[0].find("tile-1") != std::string::npos,
            "the retry writes both tiles in one patch");
    }

    // The thread writes on its own after a mark, and a stop flushes what is
    // left.
    {
        FakeStore store;
        nmos_persist::Persister p{store.io(), keys, {{0, std::nullopt}, {1, std::nullopt}}};
        p.start();
        store.actives[0] = active(S, true, D, F);
        p.mark(0);
        p.stop();
        check(store.patches.size() == 1, "a mark is written by the time stop returns");
        p.mark(1);
        check(store.patches.size() == 1, "nothing is taken after stop");
    }

    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
