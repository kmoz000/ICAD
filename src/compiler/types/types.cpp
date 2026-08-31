#include "icad/compiler/types/types.hpp"

#include <array>

namespace icad::compiler::types {
namespace {

using units::Dimension;

constexpr std::array box_properties{
    PropertySpec{"WIDTH", Dimension::length, true, true},
    PropertySpec{"DEPTH", Dimension::length, true, true},
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array cylinder_properties{
    PropertySpec{"RADIUS", Dimension::length, true, true},
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array cone_properties{
    // A manufactured cone may terminate at a mathematical apex. Feature-level
    // semantic validation keeps both radii non-negative and rejects the
    // doubly-zero degenerate case.
    PropertySpec{"RADIUS1", Dimension::length, true, false},
    PropertySpec{"RADIUS2", Dimension::length, true, false},
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array sphere_properties{
    PropertySpec{"RADIUS", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array extrude_properties{
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array revolve_properties{
    PropertySpec{"ANGLE", Dimension::angle, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array chamfer_properties{
    PropertySpec{"DISTANCE", Dimension::length, true, true},
};

constexpr std::array fillet_properties{
    PropertySpec{"RADIUS", Dimension::length, true, true},
};

constexpr std::array pattern_properties{
    PropertySpec{"SPACING", Dimension::length, true, true},
};

constexpr std::array<PropertySpec, 0> mirror_properties{};

constexpr std::array sweep_properties{
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array loft_properties{
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

constexpr std::array freeform_properties{
    PropertySpec{"HEIGHT", Dimension::length, true, true},
    PropertySpec{"TWIST", Dimension::angle, true, false},
    PropertySpec{"ORIGIN_X", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Y", Dimension::length, false, false},
    PropertySpec{"ORIGIN_Z", Dimension::length, false, false},
    PropertySpec{"ROTATION_X", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Y", Dimension::angle, false, false},
    PropertySpec{"ROTATION_Z", Dimension::angle, false, false},
};

const FeatureSchema schemas[]{
    {"BOX", box_properties},
    {"CYLINDER", cylinder_properties},
    {"CONE", cone_properties},
    {"SPHERE", sphere_properties},
    {"EXTRUDE", extrude_properties},
    {"REVOLVE", revolve_properties},
    {"CHAMFER", chamfer_properties},
    {"FILLET", fillet_properties},
    {"LINEAR_PATTERN", pattern_properties},
    {"MIRROR", mirror_properties},
    {"SWEEP", sweep_properties},
    {"LOFT", loft_properties},
    {"FREEFORM", freeform_properties},
};

} // namespace

auto find_feature_schema(std::string_view name) -> const FeatureSchema* {
    for (const auto& schema : schemas) {
        if (schema.name == name) {
            return &schema;
        }
    }
    return nullptr;
}

auto find_property(const FeatureSchema& schema, std::string_view name) -> const PropertySpec* {
    for (const auto& property : schema.properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

} // namespace icad::compiler::types
