#include "nmos/domain.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nmos_domain
{
    namespace
    {
        // string_field is the value of a top-level string field. The files
        // read here are small and flat enough that a JSON library would be
        // the larger dependency; a nested key of the same name is skipped
        // because only depth-1 keys are considered.
        std::optional<std::string> string_field(std::string const& json, char const* key)
        {
            std::string const quoted = std::string{"\""} + key + "\"";
            int depth = 0;
            bool inString = false;
            for (std::size_t i = 0; i < json.size(); ++i)
            {
                char c = json[i];
                if (inString)
                {
                    if (c == '\\') { ++i; continue; }
                    if (c == '"') inString = false;
                    continue;
                }
                if (c == '{' || c == '[') { ++depth; continue; }
                if (c == '}' || c == ']') { --depth; continue; }
                if (c != '"') continue;
                if (depth == 1 && json.compare(i, quoted.size(), quoted) == 0)
                {
                    std::size_t p = i + quoted.size();
                    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
                    if (p >= json.size() || json[p] != ':') { inString = true; continue; }
                    ++p;
                    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
                    if (p >= json.size() || json[p] != '"') return std::nullopt;
                    std::size_t end = json.find('"', p + 1);
                    if (end == std::string::npos) return std::nullopt;
                    return json.substr(p + 1, end - p - 1);
                }
                inString = true;
            }
            return std::nullopt;
        }
    }

    bool is_uuid(std::string const& s)
    {
        if (s.size() != 36) return false;
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (c != '-') return false;
                continue;
            }
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        return true;
    }

    std::optional<std::string> read_id(std::string const& domainPath, std::string& error)
    {
        std::string const path = domainPath + "/domain_def.json";
        std::ifstream file{path};
        if (!file)
        {
            error = path + " cannot be read; the platform writes it, and without "
                "it this node has no mxl_domain_id to advertise";
            return std::nullopt;
        }
        std::ostringstream buf;
        buf << file.rdbuf();
        auto id = string_field(buf.str(), "id");
        if (!id)
        {
            error = path + " has no string \"id\"";
            return std::nullopt;
        }
        if (!is_uuid(*id))
        {
            error = path + " id \"" + *id + "\" is not a lowercase UUID";
            return std::nullopt;
        }
        return id;
    }

    std::optional<std::string> flow_id_from_flow_def(std::string const& json)
    {
        auto id = string_field(json, "id");
        if (!id || !is_uuid(*id)) return std::nullopt;
        return id;
    }

    std::optional<std::string> domain_id_from_flow_def(std::string const& json)
    {
        // The tag key is unique to it, so the first string after its array
        // opens is the value; no JSON value the library writes contains it.
        static std::string const key = "\"urn:x-nvnmos:tag:mxl-domain-id\"";
        auto p = json.find(key);
        if (p == std::string::npos) return std::nullopt;
        p = json.find_first_not_of(" \t\r\n", p + key.size());
        if (p == std::string::npos || json[p] != ':') return std::nullopt;
        p = json.find_first_not_of(" \t\r\n", p + 1);
        if (p == std::string::npos || json[p] != '[') return std::nullopt;
        p = json.find_first_not_of(" \t\r\n", p + 1);
        if (p == std::string::npos || json[p] != '"') return std::nullopt;
        auto const end = json.find('"', p + 1);
        if (end == std::string::npos) return std::nullopt;
        auto id = json.substr(p + 1, end - p - 1);
        if (!is_uuid(id)) return std::nullopt;
        return id;
    }

    std::vector<Domain> discover(std::string const& primaryPath, std::string const& domainsDir,
        std::string& error)
    {
        std::vector<Domain> out;
        std::string reasons;
        std::string err;
        if (!primaryPath.empty())
        {
            if (auto id = read_id(primaryPath, err)) out.push_back({*id, primaryPath});
            else reasons += err;
        }
        std::vector<Domain> more;
        std::error_code ec;
        if (!domainsDir.empty() && std::filesystem::is_directory(domainsDir, ec))
        {
            for (auto const& entry : std::filesystem::directory_iterator{domainsDir, ec})
            {
                if (!entry.is_directory(ec)) continue;
                auto const name = entry.path().filename().string();
                auto id = read_id(entry.path().string(), err);
                if (!id || *id != name) continue;
                bool const dup = std::any_of(out.begin(), out.end(),
                    [&](Domain const& d) { return d.id == *id; });
                if (!dup) more.push_back({*id, entry.path().string()});
            }
        }
        std::sort(more.begin(), more.end(), [](Domain const& a, Domain const& b) { return a.id < b.id; });
        out.insert(out.end(), more.begin(), more.end());
        if (out.empty())
        {
            error = "no MXL domain with an identity: " + (reasons.empty() ? std::string{"none found"} : reasons) +
                (domainsDir.empty() ? "" : "; nor any under " + domainsDir);
        }
        return out;
    }

    std::string path_of(std::vector<Domain> const& domains, std::string const& id)
    {
        for (auto const& d : domains)
            if (d.id == id) return d.path;
        return {};
    }
}
