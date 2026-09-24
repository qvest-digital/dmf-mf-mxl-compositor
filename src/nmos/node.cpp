#include "nmos/node.hpp"

#include <cstdio>
#include <sstream>
#include <vector>

#include <glib.h>
#include <nvnmos.h>

#include "nmos/domain.hpp"

namespace nmos_node
{
    class Node
    {
    public:
        NvNmosNodeServer server{};
        nmos_slots::Slots* slots{nullptr};
        std::vector<nmos_domain::Domain> domains;
        std::vector<std::string> names;
        std::vector<std::string> flowDefs;
    };

    namespace
    {
        std::string receiver_name(std::size_t i) { return "tile-" + std::to_string(i); }

        // flow_def is an MXL flow definition for a tile's Receiver, the form
        // the library takes for an MXL transport file. Its "id" is the flow
        // the tile shows, or a zero UUID where nothing is connected; the
        // library reads the connected flow from IS-05, not from here. Every
        // domain id listed is one a controller may connect the Receiver in.
        std::string flow_def(std::string const& name, std::vector<std::string> const& domainIds,
            std::string const& flowId, int w, int h)
        {
            std::string ids;
            for (auto const& id : domainIds) ids += (ids.empty() ? "\"" : ",\"") + id + "\"";
            std::ostringstream o;
            o << "{\"id\":\"" << (flowId.empty() ? "00000000-0000-0000-0000-000000000000" : flowId) << "\","
              << "\"label\":\"" << name << "\","
              << "\"description\":\"compositor tile\","
              << "\"tags\":{"
              << "\"urn:x-nvnmos:tag:name\":[\"" << name << "\"],"
              << "\"urn:x-nvnmos:tag:mxl-domain-id\":[" << ids << "]},"
              << "\"format\":\"urn:x-nmos:format:video\","
              << "\"media_type\":\"video/v210\","
              << "\"grain_rate\":{\"numerator\":30000,\"denominator\":1001},"
              << "\"frame_width\":" << w << ","
              << "\"frame_height\":" << h << ","
              << "\"interlace_mode\":\"progressive\","
              << "\"colorspace\":\"BT709\","
              << "\"transfer_characteristic\":\"SDR\","
              << "\"components\":["
              << "{\"name\":\"Y\",\"width\":" << w << ",\"height\":" << h << ",\"bit_depth\":10},"
              << "{\"name\":\"Cb\",\"width\":" << w / 2 << ",\"height\":" << h << ",\"bit_depth\":10},"
              << "{\"name\":\"Cr\",\"width\":" << w / 2 << ",\"height\":" << h << ",\"bit_depth\":10}]}";
            return o.str();
        }

        void on_log(NvNmosNodeServer*, char const* categories, int level, char const* message)
        {
            g_print("nmos[%d%s%s] %s\n", level, categories && *categories ? " " : "",
                categories ? categories : "", message);
        }

        // on_activated moves a tile to the flow a controller connected, or
        // clears it. The grain rate and geometry are the Receiver's caps; the
        // reader re-reads the flow's own definition when it opens, so a flow of
        // another size is drawn at its size rather than refused here.
        bool on_activated(NvNmosNodeServer* server, NvNmosSide side, char const* name,
            char const* transportFile)
        {
            if (side != NVNMOS_SIDE_RECEIVER || name == nullptr) return false;
            auto* node = static_cast<Node*>(server->user_data);
            for (std::size_t i = 0; i < node->names.size(); ++i)
            {
                if (node->names[i] != name) continue;
                nmos_slots::Source source;
                if (transportFile != nullptr)
                {
                    auto id = nmos_domain::flow_id_from_flow_def(transportFile);
                    auto domain = nmos_domain::domain_id_from_flow_def(transportFile);
                    // IS-05 accepts only the domains this Node lists, and the
                    // first when a controller leaves the choice to it.
                    auto const& domainId = domain ? *domain : node->domains.front().id;
                    source.domain = nmos_domain::path_of(node->domains, domainId);
                    if (source.domain.empty())
                    {
                        g_printerr("nmos: %s connected in domain %s, which is not one this node reads\n",
                            name, domainId.c_str());
                        node->slots->set(i, {});
                        return false;
                    }
                    // Enabled with no flow named is a controller parking the
                    // Receiver: nothing to show, which is a cleared tile.
                    if (id) source.flow = *id;
                    else source.domain.clear();
                }
                node->slots->set(i, source);
                g_print("nmos: %s -> %s%s%s\n", name, source.flow.empty() ? "(none)" : source.flow.c_str(),
                    source.flow.empty() ? "" : " in ", source.domain.c_str());
                return true;
            }
            g_printerr("nmos: activation for unknown receiver %s\n", name);
            return false;
        }
    }

    void NodeDeleter::operator()(Node* node) const
    {
        destroy_nmos_node_server(&node->server);
        delete node;
    }

    NodePtr start(Config const& cfg, nmos_slots::Slots& slots, std::string& error)
    {
        auto domains = nmos_domain::discover(cfg.domainPath, cfg.domainsDir, error);
        if (domains.empty()) return nullptr;
        if (cfg.hostAddress.empty())
        {
            error = "no host address to advertise the Node API at";
            return nullptr;
        }
        std::vector<std::string> domainIds;
        for (auto const& d : domains) domainIds.push_back(d.id);

        NodePtr node{new Node{}};
        node->slots = &slots;
        node->domains = domains;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            node->names.push_back(receiver_name(i));
            node->flowDefs.push_back(flow_def(node->names.back(), domainIds, "",
                cfg.frameWidth, cfg.frameHeight));
        }

        std::vector<NvNmosReceiverConfig> receivers(slots.size());
        for (std::size_t i = 0; i < receivers.size(); ++i)
        {
            receivers[i].transport = NVNMOS_TRANSPORT_MXL;
            receivers[i].transport_file = node->flowDefs[i].c_str();
        }

        char const* hostAddresses[] = {cfg.hostAddress.c_str()};
        char const* functions[] = {"MXL compositor"};
        NvNmosAssetConfig asset{};
        asset.manufacturer = "DMF";
        asset.product = "dmf-mf-mxl-compositor";
        asset.instance_id = cfg.seed.c_str();
        asset.functions = functions;
        asset.num_functions = 1;

        NvNmosNetworkServicesConfig services{};
        services.domain = cfg.dnsDomain.empty() ? nullptr : cfg.dnsDomain.c_str();
        services.registration_address = cfg.registryHost.empty() ? nullptr : cfg.registryHost.c_str();
        services.registration_port = cfg.registryPort;
        services.system_address = cfg.systemHost.empty() ? nullptr : cfg.systemHost.c_str();
        services.system_port = cfg.systemPort;

        NvNmosNodeConfig nc{};
        // The address the Node was given, as its host name too. NvNmos
        // advertises every Connection API under the host name as well as the
        // addresses, host name first, and left unset that is the pod's own
        // name, which resolves nowhere outside it: a controller that tries
        // the hrefs in order stops at the first one it cannot resolve.
        nc.host_name = cfg.hostAddress.c_str();
        nc.host_addresses = hostAddresses;
        nc.num_host_addresses = 1;
        nc.http_port = cfg.httpPort;
        nc.label = cfg.label.c_str();
        nc.description = cfg.description.c_str();
        nc.asset_tags = &asset;
        nc.seed = cfg.seed.c_str();
        nc.receivers = receivers.data();
        nc.num_receivers = static_cast<unsigned int>(receivers.size());
        nc.connection_activated = &on_activated;
        nc.log_callback = &on_log;
        nc.log_level = NVNMOS_LOG_WARNING;
        nc.network_services = &services;

        node->server.user_data = node.get();
        if (!create_nmos_node_server(&nc, &node->server))
        {
            error = "the NMOS Node server did not start";
            return nullptr;
        }

        // Tiles given a flow by MXL_FLOW_IDS are showing it already, and the
        // Node says so rather than presenting them as idle.
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            auto source = slots.get(i);
            if (source.flow.empty()) continue;
            std::string domainId;
            for (auto const& d : domains)
                if (d.path == source.domain) domainId = d.id;
            if (domainId.empty()) continue;
            auto def = flow_def(node->names[i], {domainId}, source.flow, cfg.frameWidth, cfg.frameHeight);
            if (!nmos_connection_activate(&node->server, NVNMOS_SIDE_RECEIVER,
                    node->names[i].c_str(), def.c_str()))
                g_printerr("nmos: could not report %s as showing %s\n",
                    node->names[i].c_str(), source.flow.c_str());
        }

        std::string listed;
        for (auto const& d : domains) listed += (listed.empty() ? "" : ", ") + d.id + " (" + d.path + ")";
        g_print("nmos: node up at %s:%u, %zu receivers in domains %s\n",
            cfg.hostAddress.c_str(), cfg.httpPort, slots.size(), listed.c_str());
        return node;
    }

}
