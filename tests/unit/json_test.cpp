#include "icad/json/value.hpp"

#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    constexpr std::string_view source =
        R"JSON({"array":[true,false,null,-12.5e2],"emoji":"\ud83e\udd16","escaped":"line\nquote\"","object":{"x":1}})JSON";
    const auto parsed = icad::json::parse(source);
    if (!parsed.ok() || parsed.value->find("array") == nullptr ||
        parsed.value->find("emoji") == nullptr ||
        *parsed.value->find("emoji")->string() != "🤖") {
        return fail("valid JSON did not parse");
    }
    const auto serialized = icad::json::serialize(*parsed.value);
    const auto round_trip = icad::json::parse(serialized);
    if (!round_trip.ok() || icad::json::serialize(*round_trip.value) != serialized) {
        return fail("JSON serialization did not round trip");
    }
    for (const std::string_view invalid : {"{", "[1,]", "{\"x\":1,\"x\":2}",
                                           "01", "\"\\ud800\"", "true false"}) {
        if (icad::json::parse(invalid).ok()) {
            return fail("invalid JSON was accepted");
        }
    }
    const icad::json::Value constructed{icad::json::Value::Object{
        {"name", "icad"}, {"version", 0.3}, {"ready", true}}};
    if (icad::json::serialize(constructed) !=
        R"JSON({"name":"icad","ready":true,"version":0.29999999999999999})JSON") {
        return fail("constructed JSON value was not deterministic");
    }
    return 0;
}
