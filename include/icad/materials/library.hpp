#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace icad::materials {

struct Preset {
    std::string_view name;
    std::array<double, 4> base_color;
    double metallic;
    double roughness;
    std::string_view texture;
    unsigned int texture_seed;
};

[[nodiscard]] auto find(std::string_view name) -> std::optional<Preset>;
[[nodiscard]] auto all() -> std::span<const Preset>;

} // namespace icad::materials
