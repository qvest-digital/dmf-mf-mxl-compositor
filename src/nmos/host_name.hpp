// The DNS name a pod is reached at, which the NMOS Node advertises its APIs
// under.

#pragma once

#include <arpa/inet.h>

#include <istream>
#include <optional>
#include <sstream>
#include <string>

namespace nmos_host_name
{
    // <address>.<namespace>.pod.<cluster domain>, with the address's dots and
    // colons as dashes: the A record CoreDNS serves for a pod where its
    // kubernetes plugin runs with "pods insecure" or "pods verified". The
    // cluster domain is what follows "<namespace>.svc." in the resolv.conf
    // search list, of which the resolver reads the last search line. Empty
    // where that line has no such entry.
    inline std::string pod_dns_name(std::string const& address, std::string const& ns, std::istream& resolvConf)
    {
        auto const prefix = ns + ".svc.";
        std::string line;
        std::string search;
        while (std::getline(resolvConf, line))
        {
            std::istringstream words{line};
            std::string word;
            if (words >> word && word == "search") search = line;
        }
        std::istringstream words{search};
        std::string word;
        words >> word;
        while (words >> word)
        {
            if (word.compare(0, prefix.size(), prefix) != 0) continue;
            auto domain = word.substr(prefix.size());
            if (!domain.empty() && domain.back() == '.') domain.pop_back();
            if (domain.empty()) continue;
            auto name = address;
            for (auto& c : name)
                if (c == '.' || c == ':') c = '-';
            return name + "." + ns + ".pod." + domain;
        }
        return {};
    }

    // The host name the Node is given, or nothing with the reason in error:
    // explicit where set, else the pod's DNS name. NvNmos lists every API
    // under the host name first, so it has to be a DNS name a controller
    // resolves. Neither fallback is one: NvNmos's default is the pod's own
    // host name under a guessed domain, and the address as host name lists
    // every href twice. NvNmos appends a domain to a name without a dot.
    inline std::optional<std::string> node_host_name(std::string const& explicitName, std::string const& address,
                                                     std::string const& ns, std::istream& resolvConf,
                                                     std::string& error)
    {
        if (!explicitName.empty())
        {
            in6_addr buf{};
            if (::inet_pton(AF_INET, explicitName.c_str(), &buf) == 1 ||
                ::inet_pton(AF_INET6, explicitName.c_str(), &buf) == 1)
            {
                error = "NMOS_HOST_NAME is an address, not a DNS name";
                return std::nullopt;
            }
            if (explicitName.find('.') == std::string::npos)
            {
                error = "NMOS_HOST_NAME has no domain, and NvNmos would append one to it";
                return std::nullopt;
            }
            return explicitName;
        }
        if (ns.empty())
        {
            error = "NMOS_HOST_NAME or NMOS_POD_NAMESPACE is required with NMOS_HOST_ADDRESS";
            return std::nullopt;
        }
        auto name = pod_dns_name(address, ns, resolvConf);
        if (name.empty())
        {
            error = "NMOS_POD_NAMESPACE is " + ns + ", but /etc/resolv.conf searches no " + ns + ".svc. domain";
            return std::nullopt;
        }
        return name;
    }
}
