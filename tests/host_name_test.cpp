// Unit tests for forming the pod's DNS name the NMOS Node advertises its APIs
// under.

#include "nmos/host_name.hpp"

#include <cstdio>
#include <sstream>

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

    std::string name(std::string const& address, std::string const& ns, std::string const& resolvConf)
    {
        std::istringstream in{resolvConf};
        return nmos_host_name::pod_dns_name(address, ns, in);
    }
}

int main()
{
    // The search list cluster DNS gives a pod, followed by the node's own.
    auto const pod = "search lane.svc.cluster.local svc.cluster.local cluster.local example.org\n"
                     "nameserver 10.96.0.10\n"
                     "options ndots:5\n";
    check(name("10.244.5.119", "lane", pod) == "10-244-5-119.lane.pod.cluster.local",
          "an IPv4 address becomes the pod's DNS name");

    // The cluster domain is the cluster's, not a fixed cluster.local.
    check(name("10.0.0.7", "lane", "search lane.svc.k8s.example svc.k8s.example k8s.example\n") ==
              "10-0-0-7.lane.pod.k8s.example",
          "the cluster domain is read from the search list");

    check(name("fd00::1", "lane", pod) == "fd00--1.lane.pod.cluster.local", "an IPv6 address has its colons dashed");
    check(name("10.0.0.7", "lane", "search lane.svc.cluster.local.\n") == "10-0-0-7.lane.pod.cluster.local",
          "a trailing dot is dropped");

    // Another namespace's entry, or none, gives no name: a guessed domain would
    // advertise a host no controller resolves.
    check(name("10.0.0.7", "lane", "search other.svc.cluster.local svc.cluster.local\n").empty(),
          "another namespace's search entry is not taken");
    check(name("10.0.0.7", "lane", "nameserver 10.96.0.10\n").empty(), "no search list gives no name");
    check(name("10.0.0.7", "lane", "search lane.svc.\n").empty(), "an empty cluster domain gives no name");
    check(name("10.0.0.7", "lane", "search lane.svc..\n").empty(), "a lone dot is no cluster domain");
    // The resolver reads the last search line only.
    check(name("10.0.0.7", "lane", "search lane.svc.old.example\nsearch lane.svc.cluster.local\n") ==
              "10-0-0-7.lane.pod.cluster.local",
          "the last search line is the one read");
    check(name("10.0.0.7", "lane", "search lane.svc.cluster.local\nsearch example.org\n").empty(),
          "an entry on an earlier search line is not read");

    // Which host name the Node is given: an explicit one first, then the
    // pod's, and never NvNmos's default or the address.
    auto host = [](std::string const& explicitName, std::string const& ns, std::string const& resolvConf,
                   std::string& error) {
        std::istringstream in{resolvConf};
        error.clear();
        return nmos_host_name::node_host_name(explicitName, "10.244.5.119", ns, in, error);
    };
    std::string error;
    auto got = host("node.lane.example", "lane", pod, error);
    check(got && *got == "node.lane.example", "an explicit host name takes precedence over the pod's");
    got = host("node.lane.example", "", "", error);
    check(got && *got == "node.lane.example", "an explicit host name needs no namespace or search list");
    got = host("", "lane", pod, error);
    check(got && *got == "10-244-5-119.lane.pod.cluster.local", "without one, a namespace gives the pod's DNS name");
    got = host("", "", pod, error);
    check(!got && error.find("NMOS_HOST_NAME or NMOS_POD_NAMESPACE") != std::string::npos,
          "neither is refused, naming both");
    got = host("", "lane", "search other.svc.cluster.local\n", error);
    check(!got && error.find("lane.svc.") != std::string::npos,
          "a namespace the search list does not name is refused, not guessed");
    got = host("10.244.5.119", "lane", pod, error);
    check(!got && error.find("address") != std::string::npos, "an IPv4 address is not a host name");
    got = host("fd00::1", "lane", pod, error);
    check(!got && error.find("address") != std::string::npos, "an IPv6 address is not a host name");
    got = host("compositor", "lane", pod, error);
    check(!got && error.find("domain") != std::string::npos, "a name without a domain is refused");

    if (failures == 0) std::puts("host_name_test: ok");
    return failures == 0 ? 0 : 1;
}
