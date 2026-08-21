#pragma once

#include <optional>
#include <string_view>

namespace icad::compiler::units {

enum class Dimension { dimensionless, length, angle, mass, time, unknown };

struct UnitDefinition {
    std::string_view symbol;
    Dimension dimension{Dimension::unknown};
    double scale_to_canonical{1.0};
};

[[nodiscard]] auto find(std::string_view symbol) -> std::optional<UnitDefinition>;
[[nodiscard]] auto canonical_symbol(Dimension dimension) -> std::string_view;
[[nodiscard]] auto convert(double value, UnitDefinition from, UnitDefinition to)
    -> std::optional<double>;

} // namespace icad::compiler::units
