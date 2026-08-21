#include "icad/compiler/units/units.hpp"

#include <array>

namespace icad::compiler::units {
namespace {

constexpr std::array definitions{
    UnitDefinition{"mm", Dimension::length, 1.0},
    UnitDefinition{"cm", Dimension::length, 10.0},
    UnitDefinition{"m", Dimension::length, 1000.0},
    UnitDefinition{"in", Dimension::length, 25.4},
    UnitDefinition{"ft", Dimension::length, 304.8},
    UnitDefinition{"deg", Dimension::angle, 1.0},
    UnitDefinition{"rad", Dimension::angle, 57.29577951308232},
    UnitDefinition{"g", Dimension::mass, 1.0},
    UnitDefinition{"kg", Dimension::mass, 1000.0},
    UnitDefinition{"ms", Dimension::time, 0.001},
    UnitDefinition{"s", Dimension::time, 1.0},
    UnitDefinition{"min", Dimension::time, 60.0},
};

} // namespace

auto find(std::string_view symbol) -> std::optional<UnitDefinition> {
    for (const auto& definition : definitions) {
        if (definition.symbol == symbol) {
            return definition;
        }
    }
    return std::nullopt;
}

auto canonical_symbol(Dimension dimension) -> std::string_view {
    switch (dimension) {
    case Dimension::dimensionless:
        return "1";
    case Dimension::length:
        return "mm";
    case Dimension::angle:
        return "deg";
    case Dimension::mass:
        return "g";
    case Dimension::time:
        return "s";
    case Dimension::unknown:
        return "unknown";
    }
    return "unknown";
}

auto convert(double value, UnitDefinition from, UnitDefinition to) -> std::optional<double> {
    if (from.dimension != to.dimension) {
        return std::nullopt;
    }
    return value * from.scale_to_canonical / to.scale_to_canonical;
}

} // namespace icad::compiler::units
