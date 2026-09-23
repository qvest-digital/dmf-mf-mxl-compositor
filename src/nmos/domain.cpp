#include "nmos/domain.hpp"

#include <cctype>
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
}
