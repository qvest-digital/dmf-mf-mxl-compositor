#include "nmos/configmap.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "nmos/connections.hpp"

namespace nmos_configmap
{
    namespace
    {
        std::string const serviceAccount = "/var/run/secrets/kubernetes.io/serviceaccount";

        bool read_file(std::string const& path, std::string& out)
        {
            std::ifstream f{path};
            if (!f) return false;
            std::ostringstream buf;
            buf << f.rdbuf();
            out = buf.str();
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
            return true;
        }

        std::string failure(std::string const& method, std::string const& name, nmos_http::Response const& res)
        {
            return method + " configmap " + name + ": " +
                (res.status == 0 ? res.error : "HTTP " + std::to_string(res.status) + " " + res.body.substr(0, 300));
        }
    }

    ConfigMap::ConfigMap(std::string name) : name_{std::move(name)} {}

    nmos_http::Response ConfigMap::send(nmos_http::Request req) const
    {
        nmos_http::Response res;
        char const* host = std::getenv("KUBERNETES_SERVICE_HOST");
        char const* port = std::getenv("KUBERNETES_SERVICE_PORT");
        if (host == nullptr || *host == '\0')
        {
            res.error = "KUBERNETES_SERVICE_HOST is not set";
            return res;
        }
        std::string ns, token;
        if (!read_file(serviceAccount + "/namespace", ns) || ns.empty() ||
            !read_file(serviceAccount + "/token", token))
        {
            res.error = "no service account namespace and token in " + serviceAccount;
            return res;
        }
        std::string h{host};
        if (h.find(':') != std::string::npos) h = "[" + h + "]";
        req.url = "https://" + h + ":" + (port && *port ? port : "443") + "/api/v1/namespaces/" + ns +
            "/configmaps/" + name_;
        req.headers.push_back("Authorization: Bearer " + token);
        req.headers.push_back("Accept: application/json");
        req.caFile = serviceAccount + "/ca.crt";
        return nmos_http::send(req);
    }

    bool ConfigMap::load(std::map<std::string, std::string>& out, long timeoutSeconds, long& status,
        std::string& error) const
    {
        nmos_http::Request req;
        req.timeoutSeconds = timeoutSeconds;
        auto res = send(req);
        status = res.status;
        if (res.status != 200)
        {
            error = failure("GET", name_, res);
            return false;
        }
        auto data = nmos_connections::configmap_data(res.body);
        if (!data)
        {
            error = "GET configmap " + name_ + ": the response is not a ConfigMap";
            return false;
        }
        out = std::move(*data);
        return true;
    }

    bool ConfigMap::patch(std::string const& body, std::string& error) const
    {
        nmos_http::Request req;
        req.method = "PATCH";
        req.headers = {"Content-Type: application/merge-patch+json"};
        req.body = body;
        auto res = send(req);
        if (res.status == 200) return true;
        error = failure("PATCH", name_, res);
        return false;
    }
}
