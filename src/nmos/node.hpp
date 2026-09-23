// The compositor's NMOS Node: IS-04 registration and an IS-05 Connection API
// with one BCP-007-03 MXL Receiver per tile.
//
// A controller connects a tile by activating its Receiver with an mxl_flow_id;
// the tile's slot takes that flow and the reader worker reopens on it.
// Deactivating a Receiver clears the slot and the tile goes black.

#pragma once

#include <memory>
#include <string>

#include "nmos/slots.hpp"

namespace nmos_node
{
    struct Config
    {
        // Required. The MXL domain directory; its domain_def.json names the
        // domain every Receiver advertises.
        std::string domainPath;
        // Required. The address this Node's APIs are reached at, which the
        // registry hands to controllers.
        std::string hostAddress;
        // Port for the Node and Connection APIs. 0 takes the library default.
        unsigned int httpPort{0};
        // Seed for stable resource ids across restarts. Two instances
        // sharing a seed publish the same ids, so each needs its own.
        std::string seed;
        std::string label;
        std::string description;
        // DNS-SD search domain to browse for a registry. Empty lets the
        // library choose; ignored where registryHost is set.
        std::string dnsDomain;
        // A fixed IS-04 Registration API, which disables discovery.
        std::string registryHost;
        unsigned int registryPort{0};
        // A fixed IS-09 System API, used only beside a fixed registry. Where
        // it is not set the Node keeps looking for one over DNS-SD, which is
        // what a registry named by address is there to avoid.
        std::string systemHost;
        unsigned int systemPort{0};
        // Tile geometry and rate, stated in each Receiver's caps.
        int frameWidth{1920};
        int frameHeight{1080};
    };

    class Node;

    // Owns a running Node; destroying it deregisters and shuts it down.
    struct NodeDeleter
    {
        void operator()(Node* node) const;
    };
    using NodePtr = std::unique_ptr<Node, NodeDeleter>;

    // start creates the Node, registers one Receiver per slot and marks the
    // Receivers of slots that already name a flow as active, so an instance
    // configured with MXL_FLOW_IDS reports what it shows. Fails, with the
    // reason in error, where the domain has no identity: an MXL Receiver
    // whose domain is unconstrained is one no controller can route to.
    NodePtr start(Config const& cfg, nmos_slots::Slots& slots, std::string& error);
}
