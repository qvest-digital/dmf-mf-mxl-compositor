// A tile's IS-05 connection as it is kept across restarts, and the JSON that
// carries it: a Receiver's /active, the value stored per tile, the /staged
// PATCH that restores it, and the ConfigMap documents it is stored in.
//
// JSON only, so tests/connections_test.cpp covers it without the NMOS
// library, libmxl or a network.

#pragma once

#include <map>
#include <optional>
#include <string>

namespace nmos_connections
{
    // A Receiver connected to a flow: master_enable true with an mxl_flow_id.
    // senderId and domainId are empty where /active holds null.
    struct Connection
    {
        std::string senderId;
        std::string domainId;
        std::string flowId;

        bool operator==(Connection const& o) const
        {
            return senderId == o.senderId && domainId == o.domainId && flowId == o.flowId;
        }
        bool operator!=(Connection const& o) const { return !(*this == o); }
    };

    // from_active reads an IS-05 Receiver /active body, or a value to_json
    // wrote. Nothing where the Receiver is not connected to a flow
    // (master_enable false, or no mxl_flow_id), and nothing where a field is
    // not what IS-05 allows, so a damaged value is never replayed.
    std::optional<Connection> from_active(std::string const& json);

    // to_json is the stored value: the IS-05 fields that make up the
    // connection, in the shape of a /staged PATCH body.
    std::string to_json(Connection const& c);

    // staged_patch is the IS-05 /staged PATCH body that activates c at once.
    std::string staged_patch(Connection const& c);

    // configmap_data is the string values under a ConfigMap's top-level
    // "data", keyed by their keys, and empty where it has none. Nothing
    // where the document is not a JSON object, so an unreadable ConfigMap is
    // not taken for one that stores nothing.
    std::optional<std::map<std::string, std::string>> configmap_data(std::string const& json);

    // data_patch is a JSON merge patch setting each key under "data" to its
    // value, or removing it where the value is nothing.
    std::string data_patch(std::map<std::string, std::optional<std::string>> const& values);
}
