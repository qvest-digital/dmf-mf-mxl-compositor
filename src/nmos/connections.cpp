#include "nmos/connections.hpp"

#include <nlohmann/json.hpp>

#include "nmos/domain.hpp"

namespace nmos_connections
{
    namespace
    {
        using nlohmann::json;

        json parse(std::string const& text)
        {
            return json::parse(text, nullptr, false);
        }

        // uuid_or_null reads a member that is a UUID or null into out, empty
        // for null or, where it is not required, absent.
        bool uuid_or_null(json const& o, char const* key, bool required, std::string& out)
        {
            out.clear();
            auto it = o.find(key);
            if (it == o.end()) return !required;
            if (it->is_null()) return true;
            if (!it->is_string()) return false;
            out = it->get<std::string>();
            return nmos_domain::is_uuid(out);
        }

        json or_null(std::string const& s)
        {
            return s.empty() ? json(nullptr) : json(s);
        }

        json fields(Connection const& c)
        {
            return {
                {"sender_id", or_null(c.senderId)},
                {"master_enable", true},
                {"transport_params", json::array({{{"mxl_domain_id", or_null(c.domainId)},
                                                   {"mxl_flow_id", or_null(c.flowId)}}})},
            };
        }
    }

    std::optional<Connection> from_active(std::string const& text)
    {
        auto const active = parse(text);
        if (!active.is_object()) return std::nullopt;
        auto enable = active.find("master_enable");
        if (enable == active.end() || !enable->is_boolean() || !enable->get<bool>()) return std::nullopt;

        Connection c;
        if (!uuid_or_null(active, "sender_id", false, c.senderId)) return std::nullopt;
        auto params = active.find("transport_params");
        if (params == active.end() || !params->is_array() || params->empty() || !(*params)[0].is_object())
            return std::nullopt;
        auto const& leg = (*params)[0];
        if (!uuid_or_null(leg, "mxl_flow_id", true, c.flowId) || c.flowId.empty()) return std::nullopt;
        if (!uuid_or_null(leg, "mxl_domain_id", false, c.domainId)) return std::nullopt;
        return c;
    }

    std::string to_json(Connection const& c)
    {
        return fields(c).dump();
    }

    std::string staged_patch(Connection const& c)
    {
        auto body = fields(c);
        body["activation"] = {{"mode", "activate_immediate"}};
        return body.dump();
    }

    std::optional<std::map<std::string, std::string>> configmap_data(std::string const& text)
    {
        auto const cm = parse(text);
        if (!cm.is_object()) return std::nullopt;
        std::map<std::string, std::string> out;
        auto data = cm.find("data");
        if (data == cm.end() || data->is_null()) return out;
        if (!data->is_object()) return std::nullopt;
        for (auto const& [key, value] : data->items())
            if (value.is_string()) out.emplace(key, value.get<std::string>());
        return out;
    }

    std::string data_patch(std::map<std::string, std::optional<std::string>> const& values)
    {
        json data = json::object();
        for (auto const& [key, value] : values) data[key] = value ? json(*value) : json(nullptr);
        return json{{"data", data}}.dump();
    }
}
