// The MXL domain identity, from <domain>/domain_def.json.
//
// BCP-007-03 has every MXL domain carry its UUID there, and a receiver
// advertises it as mxl_domain_id. The platform writes the file; a function
// only reads it.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nmos_domain
{
    // read_id returns the domain's id, or nothing with the reason in error.
    // A missing file, an unparsable one and an id that is not a lowercase UUID
    // are all errors: an MXL receiver with no domain to name is one a
    // controller cannot route to.
    std::optional<std::string> read_id(std::string const& domainPath, std::string& error);

    // is_uuid reports whether s is a lowercase RFC 4122 UUID, the form
    // BCP-007-03 gives mxl_domain_id and mxl_flow_id.
    bool is_uuid(std::string const& s);

    // flow_id_from_flow_def is the top-level "id" of an MXL flow definition,
    // which is how the library hands a receiver the mxl_flow_id it was
    // connected to. Nothing where it is absent or not a UUID.
    std::optional<std::string> flow_id_from_flow_def(std::string const& json);

    // domain_id_from_flow_def is the first urn:x-nvnmos:tag:mxl-domain-id
    // value of an MXL flow definition, which is how the library hands a
    // receiver the mxl_domain_id it was connected in. Nothing where it is
    // absent or not a UUID.
    std::optional<std::string> domain_id_from_flow_def(std::string const& json);

    // Domain is one MXL domain this process can read: its id and directory.
    struct Domain
    {
        std::string id;
        std::string path;
    };

    // discover is every MXL domain readable here: primaryPath first where it
    // carries an identity, then each <domainsDir>/<id> whose domain_def.json
    // carries that same <id>, in id order. A directory whose file names
    // another id is skipped, since reading it as that domain would show
    // another domain's flow of the same id. Empty, with the reasons in error,
    // where there is none.
    std::vector<Domain> discover(std::string const& primaryPath, std::string const& domainsDir,
        std::string& error);

    // path_of is the directory of the domain id names, empty where it is not
    // one of domains.
    std::string path_of(std::vector<Domain> const& domains, std::string const& id);
}
