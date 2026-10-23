// Unit tests for reading the fields a tile checks from an MXL flow definition.

#include "flow_def.hpp"

#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool ok, char const* what)
    {
        if (!ok)
        {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }
}

int main()
{
    // A writer that escapes the solidus still writes v210: a tile refusing it
    // stays black on a connection the controller was told succeeded.
    auto const escaped = flow_def::parse(
        R"({"format":"urn:x-nmos:format:video","media_type":"video\/v210","frame_width":1920,"frame_height":1080})");
    check(escaped.mediaType == "video/v210", "escaped solidus is decoded");
    check(escaped.frameWidth == 1920 && escaped.frameHeight == 1080, "frame size is read");

    auto const plain = flow_def::parse(R"({"media_type": "audio/float32"})");
    check(plain.mediaType == "audio/float32", "unescaped media type is read");

    // The components carry a width and height of their own, which must not
    // stand in for an absent frame size.
    auto const nested = flow_def::parse(
        R"({"media_type":"video/v210","components":[{"name":"Y","width":1920,"height":1080}]})");
    check(nested.frameWidth == -1 && nested.frameHeight == -1, "component size is not the frame size");

    auto const wrongType = flow_def::parse(R"({"media_type":1,"frame_width":"1920"})");
    check(wrongType.mediaType.empty() && wrongType.frameWidth == -1, "fields of another type are ignored");

    auto const garbage = flow_def::parse("not json");
    check(garbage.mediaType.empty() && garbage.frameWidth == -1, "a definition that is not JSON gives defaults");

    if (failures == 0) std::puts("flow_def_test: ok");
    return failures == 0 ? 0 : 1;
}
