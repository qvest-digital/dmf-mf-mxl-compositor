#include "nmos/http.hpp"

#include <mutex>

#include <curl/curl.h>

namespace nmos_http
{
    namespace
    {
        size_t collect(char* data, size_t size, size_t n, void* out)
        {
            static_cast<std::string*>(out)->append(data, size * n);
            return size * n;
        }
    }

    Response send(Request const& req)
    {
        // curl_global_init is not thread-safe, and the first request may come
        // from any of the threads that use this.
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

        Response res;
        CURL* curl = curl_easy_init();
        if (curl == nullptr)
        {
            res.error = "curl_easy_init failed";
            return res;
        }
        curl_slist* headers = nullptr;
        for (auto const& h : req.headers) headers = curl_slist_append(headers, h.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        if (!req.body.empty() || req.method == "PATCH" || req.method == "POST" || req.method == "PUT")
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (!req.caFile.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, req.caFile.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        // Signals would interrupt other threads; timeouts work without them.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        // Both peers are reached directly: the Connection API on loopback,
        // the API server at its in-cluster address.
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

        CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
        else res.error = curl_easy_strerror(rc);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return res;
    }
}
