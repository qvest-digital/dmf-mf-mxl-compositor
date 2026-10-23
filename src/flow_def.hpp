// The fields of an MXL flow definition a tile depends on.
//
// The definition is the writer's JSON, parsed as JSON: a writer may escape
// any character in a string, and one that serializes through cpprestsdk
// writes "video\/v210".

#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace flow_def
{
    struct Fields
    {
        std::string mediaType;
        int frameWidth = -1;
        int frameHeight = -1;
    };

    // A field that is absent or of another type keeps its default, as does
    // every field of a definition that is not JSON.
    inline Fields parse(std::string const& text)
    {
        Fields out;
        auto const j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object()) return out;
        auto const str = j.find("media_type");
        if (str != j.end() && str->is_string()) out.mediaType = str->get<std::string>();
        auto const num = [&j](char const* key) {
            auto const it = j.find(key);
            return it != j.end() && it->is_number_integer() ? it->get<int>() : -1;
        };
        out.frameWidth = num("frame_width");
        out.frameHeight = num("frame_height");
        return out;
    }
}
