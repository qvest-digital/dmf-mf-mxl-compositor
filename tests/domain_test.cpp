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

    // A slot holds the flow a tile shows; unknown slots are refused.
    nmos_slots::Slots slots{{"a", ""}};
    check(slots.get(0) == "a" && slots.get(1).empty(), "slots start from the configured flows");
    check(slots.set(1, "b") && slots.get(1) == "b", "a slot takes a connected flow");
    check(slots.set(0, "") && slots.get(0).empty(), "a slot is cleared on deactivation");
    check(!slots.set(2, "c"), "a slot that does not exist is refused");

    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
