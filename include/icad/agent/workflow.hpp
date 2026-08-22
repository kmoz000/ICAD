#pragma once

#include <string>
#include <string_view>

namespace icad::agent {

enum class DesignIntent { robotic_arm, bridge, generic_part };

struct BootstrapResult {
    DesignIntent intent{DesignIntent::generic_part};
    std::string source;
    std::string json;
};

[[nodiscard]] auto bootstrap(std::string_view prompt) -> BootstrapResult;
[[nodiscard]] auto conceptualize_json(std::string_view prompt) -> std::string;
[[nodiscard]] auto review_json(std::string_view source) -> std::string;
[[nodiscard]] auto intent_name(DesignIntent intent) -> std::string_view;

} // namespace icad::agent
