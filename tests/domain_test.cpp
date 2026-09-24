// Unit tests for the parts of the NMOS integration that decide what is
// published: the domain identity and the flow a controller connected. Built
// without the NMOS library or libmxl, so they run anywhere the compiler does.

#include "nmos/domain.hpp"
#include "nmos/slots.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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

    std::string write_domain(std::string const& body)
    {
        auto dir = std::filesystem::temp_directory_path() /
            ("mxl-domain-" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        if (!body.empty()) std::ofstream{dir / "domain_def.json"} << body;
        return dir.string();
    }
}

int main()
{
    std::string err;

    // The id BCP-007-03 puts in domain_def.json is what every Receiver names.
    auto ok = nmos_domain::read_id(write_domain(
        R"({"id":"6f1c2a3b-4d5e-4f60-8a71-b2c3d4e5f607","label":"d","description":"","tags":{}})"), err);
    check(ok && *ok == "6f1c2a3b-4d5e-4f60-8a71-b2c3d4e5f607", "a domain_def.json id is read");

    // An id nested under another key is not the domain's.
    auto nested = nmos_domain::read_id(write_domain(
        R"({"tags":{"id":"6f1c2a3b-4d5e-4f60-8a71-b2c3d4e5f607"},"label":"d"})"), err);
    check(!nested, "an id inside tags is not taken for the domain's");

    // No file: nothing to advertise, and the reason names the file.
    auto missing = nmos_domain::read_id(write_domain(""), err);
    check(!missing && err.find("domain_def.json") != std::string::npos,
        "a missing domain_def.json is an error naming the file");

    // Not a UUID, or not lowercase: a controller would refuse the value.
    check(!nmos_domain::read_id(write_domain(R"({"id":"n06"})"), err), "a node name is refused");
    check(!nmos_domain::read_id(write_domain(
        R"({"id":"6F1C2A3B-4D5E-4F60-8A71-B2C3D4E5F607"})"), err), "an uppercase UUID is refused");

    // The flow a controller connected arrives as the flow definition's id.
    auto flow = nmos_domain::flow_id_from_flow_def(
        R"({"id":"11111111-2222-4333-8444-555555555555","tags":{"urn:x-nvnmos:tag:name":["tile-0"]}})");
    check(flow && *flow == "11111111-2222-4333-8444-555555555555", "the connected flow id is read");
    check(!nmos_domain::flow_id_from_flow_def(R"({"label":"x"})"), "no id is no flow");

    // A slot holds the flow a tile shows and the domain it is in; unknown
    // slots are refused.
    nmos_slots::Slots slots{{{"/run/mxl/domain", "a"}, {}}};
    check(slots.get(0).flow == "a" && slots.get(1).flow.empty(), "slots start from the configured flows");
    check(slots.set(1, {"/d2", "b"}) && slots.get(1).flow == "b" && slots.get(1).domain == "/d2",
        "a slot takes a connected flow and its domain");
    check(slots.get(1) != nmos_slots::Source{"/run/mxl/domain", "b"},
        "the same flow id in another domain is another source");
    check(slots.set(0, {}) && slots.get(0).flow.empty(), "a slot is cleared on deactivation");
    check(!slots.set(2, {"/d", "c"}), "a slot that does not exist is refused");

    // The domain a controller connected in arrives as the flow definition's
    // mxl-domain-id tag.
    auto dom = nmos_domain::domain_id_from_flow_def(
        R"({"id":"11111111-2222-4333-8444-555555555555","tags":{"urn:x-nvnmos:tag:name":["tile-0"],)"
        R"("urn:x-nvnmos:tag:mxl-domain-id": [ "fec11c1b-9fab-4275-ab2c-ef676fa2e081" ]}})");
    check(dom && *dom == "fec11c1b-9fab-4275-ab2c-ef676fa2e081", "the connected domain id is read");
    check(!nmos_domain::domain_id_from_flow_def(R"({"tags":{}})"), "no tag is no domain");
    check(!nmos_domain::domain_id_from_flow_def(
        R"({"tags":{"urn:x-nvnmos:tag:mxl-domain-id":["studio"]}})"), "a domain name is not an id");

    // Every domain readable here is found: the primary first, then each
    // domains/<id> whose file carries that id. One whose file names another
    // id is left out, since reading it as that domain would show another
    // domain's flow of the same id.
    {
        auto root = std::filesystem::temp_directory_path() / ("mxl-root-" + std::to_string(std::rand()));
        auto put = [](std::filesystem::path const& dir, std::string const& id) {
            std::filesystem::create_directories(dir);
            std::ofstream{dir / "domain_def.json"} << R"({"id":")" << id << R"("})";
        };
        std::string const a = "462050c6-eea6-475b-a1f6-cb3ef2a6cce5";
        std::string const b = "fec11c1b-9fab-4275-ab2c-ef676fa2e081";
        std::string const c = "0a000000-0000-4000-8000-000000000001";
        put(root / "domain", a);
        put(root / "domains" / b, b);
        put(root / "domains" / c, a);
        std::string e;
        auto found = nmos_domain::discover((root / "domain").string(), (root / "domains").string(), e);
        check(found.size() == 2, "the primary and one matching domain are found");
        check(found.size() == 2 && found[0].id == a && found[1].id == b, "the primary is listed first");
        check(nmos_domain::path_of(found, b) == (root / "domains" / b).string(), "an id maps to its directory");
        check(nmos_domain::path_of(found, c).empty(), "a mismatched directory is not a domain");

        auto none = nmos_domain::discover((root / "absent").string(), (root / "absent-too").string(), e);
        check(none.empty() && !e.empty(), "no domain at all is an error with a reason");
        auto onlyMore = nmos_domain::discover("", (root / "domains").string(), e);
        check(onlyMore.size() == 1 && onlyMore[0].id == b, "domains alone, with no primary, are enough");
    }

    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
