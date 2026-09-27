// One Kubernetes ConfigMap in the pod's own namespace, read and merge-patched
// through the API server with the pod's service account.
//
// The API server is taken from KUBERNETES_SERVICE_HOST and
// KUBERNETES_SERVICE_PORT, the namespace, token and CA from
// /var/run/secrets/kubernetes.io/serviceaccount, as every pod has them. The
// token is read per request because the kubelet rotates it.

#pragma once

#include <map>
#include <string>

#include "nmos/http.hpp"

namespace nmos_configmap
{
    class ConfigMap
    {
    public:
        explicit ConfigMap(std::string name);

        // load reads the ConfigMap's data into out. False with the reason in
        // error where it could not be read, including where it does not exist
        // or is not a ConfigMap; status is the HTTP status, 0 where none
        // arrived.
        bool load(std::map<std::string, std::string>& out, long timeoutSeconds, long& status,
            std::string& error) const;

        // patch applies a JSON merge patch. False with the reason in error.
        bool patch(std::string const& body, std::string& error) const;

    private:
        // send completes req with the ConfigMap's URL and the credentials,
        // and hands back the response.
        nmos_http::Response send(nmos_http::Request req) const;

        std::string name_;
    };
}
