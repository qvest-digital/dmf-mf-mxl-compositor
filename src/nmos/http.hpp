// A blocking HTTP(S) request over libcurl, for the Connection API this process
// serves and for the Kubernetes API.

#pragma once

#include <string>
#include <vector>

namespace nmos_http
{
    struct Request
    {
        std::string method{"GET"};
        std::string url;
        std::vector<std::string> headers;
        std::string body;
        // CA bundle to verify the server against; empty for the system's.
        std::string caFile;
        long timeoutSeconds{10};
    };

    struct Response
    {
        // 0 where no response arrived, with the reason in error.
        long status{0};
        std::string body;
        std::string error;
    };

    Response send(Request const& req);
}
