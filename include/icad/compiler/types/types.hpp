#pragma once

#include "icad/compiler/units/units.hpp"

#include <span>
#include <string_view>

namespace icad::compiler::types {

struct PropertySpec {
    std::string_view name;
    units::Dimension dimension{units::Dimension::unknown};
    bool required{true};
    bool must_be_positive{true};
};

struct FeatureSchema {
    std::string_view name;
    std::span<const PropertySpec> properties;
};

[[nodiscard]] auto find_feature_schema(std::string_view name) -> const FeatureSchema*;
[[nodiscard]] auto find_property(const FeatureSchema& schema, std::string_view name)
    -> const PropertySpec*;

} // namespace icad::compiler::types

