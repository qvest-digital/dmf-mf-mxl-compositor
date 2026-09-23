// The MXL domain identity, from <domain>/domain_def.json.
//
// BCP-007-03 has every MXL domain carry its UUID there, and a receiver
// advertises it as mxl_domain_id. The platform writes the file; a function
// only reads it.

#pragma once

#include <optional>
#include <string>

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
}
