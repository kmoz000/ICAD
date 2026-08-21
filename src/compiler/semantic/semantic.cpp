#include "icad/compiler/semantic/semantic.hpp"

#include "icad/compiler/types/types.hpp"
#include "icad/compiler/units/units.hpp"
#include "icad/constraints/sketch_solver.hpp"
#include "icad/materials/library.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace icad::compiler {
namespace {

auto add_error(SemanticResult& result, std::string code, std::string message,
               SourceLocation location) -> void {
    result.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::error,
        std::move(code),
        std::move(message),
        location,
    });
}

[[nodiscard]] auto lower_quantity(const ast::QuantityLiteral& literal, units::Dimension expected,
                                  bool positive, SemanticResult& result) -> ir::Quantity {
    const auto source_unit = units::find(literal.unit);
    if (!source_unit) {
        add_error(result, "ICAD-S0001", "unknown unit '" + literal.unit + "'", literal.location);
        return {};
    }
    if (expected != units::Dimension::unknown && source_unit->dimension != expected) {
        add_error(result, "ICAD-S0002",
                  "unit '" + literal.unit + "' has the wrong physical dimension", literal.location);
        return {};
    }
    if (positive && literal.value <= 0.0) {
        add_error(result, "ICAD-S0003", "quantity must be greater than zero", literal.location);
    }
    const std::string_view canonical = units::canonical_symbol(source_unit->dimension);
    const auto target_unit = units::find(canonical);
    const auto converted = target_unit ? units::convert(literal.value, *source_unit, *target_unit)
                                       : std::optional<double>{};
    if (!converted) {
        add_error(result, "ICAD-S0004", "quantity cannot be converted to canonical units",
                  literal.location);
        return {};
    }
    return ir::Quantity{*converted, std::string{canonical}, source_unit->dimension};
}

[[nodiscard]] auto lower_value(const ast::ValueDecl& value, units::Dimension expected,
                               const std::unordered_map<std::string, ir::Quantity>& named_values,
                               SemanticResult& result) -> ir::Quantity {
    if (value.parameter_reference.empty()) {
        return lower_quantity(value.literal, expected, false, result);
    }
    const auto found = named_values.find(value.parameter_reference);
    if (found == named_values.end()) {
        add_error(result, "ICAD-S0031",
                  "unknown scalar or angle '" + value.parameter_reference + "'", value.location);
        return {};
    }
    if (found->second.dimension != expected) {
        add_error(result, "ICAD-S0031",
                  "'" + value.parameter_reference + "' has the wrong physical dimension",
                  value.location);
    }
    return found->second;
}

[[nodiscard]] auto signed_area(const std::vector<ir::Point2>& points) -> double {
    double area = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& first = points[index];
        const auto& second = points[(index + 1) % points.size()];
        area += first.x_mm * second.y_mm - second.x_mm * first.y_mm;
    }
    return area * 0.5;
}

[[nodiscard]] auto orientation(const ir::Point2& a, const ir::Point2& b, const ir::Point2& c)
    -> double {
    return (b.x_mm - a.x_mm) * (c.y_mm - a.y_mm) - (b.y_mm - a.y_mm) * (c.x_mm - a.x_mm);
}

[[nodiscard]] auto segments_cross(const ir::Point2& a, const ir::Point2& b, const ir::Point2& c,
                                  const ir::Point2& d) -> bool {
    constexpr double tolerance = 1e-9;
    const double first = orientation(a, b, c);
    const double second = orientation(a, b, d);
    const double third = orientation(c, d, a);
    const double fourth = orientation(c, d, b);
    const bool proper =
        ((first > tolerance && second < -tolerance) ||
         (first < -tolerance && second > tolerance)) &&
        ((third > tolerance && fourth < -tolerance) || (third < -tolerance && fourth > tolerance));
    if (proper)
        return true;
    const auto on_segment = [](const ir::Point2& first_point, const ir::Point2& second_point,
                               const ir::Point2& candidate) {
        constexpr double bounds_tolerance = 1e-9;
        return candidate.x_mm >= std::min(first_point.x_mm, second_point.x_mm) - bounds_tolerance &&
               candidate.x_mm <= std::max(first_point.x_mm, second_point.x_mm) + bounds_tolerance &&
               candidate.y_mm >= std::min(first_point.y_mm, second_point.y_mm) - bounds_tolerance &&
               candidate.y_mm <= std::max(first_point.y_mm, second_point.y_mm) + bounds_tolerance;
    };
    return (std::abs(first) <= tolerance && on_segment(a, b, c)) ||
           (std::abs(second) <= tolerance && on_segment(a, b, d)) ||
           (std::abs(third) <= tolerance && on_segment(c, d, a)) ||
           (std::abs(fourth) <= tolerance && on_segment(c, d, b));
}

[[nodiscard]] auto self_intersects(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t first = 0; first < points.size(); ++first) {
        const std::size_t first_next = (first + 1) % points.size();
        for (std::size_t second = first + 1; second < points.size(); ++second) {
            const std::size_t second_next = (second + 1) % points.size();
            if (first == second || first_next == second || second_next == first) {
                continue;
            }
            if (segments_cross(points[first], points[first_next], points[second],
                               points[second_next])) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] auto same_point(const ir::Point2& first, const ir::Point2& second) -> bool {
    return std::hypot(first.x_mm - second.x_mm, first.y_mm - second.y_mm) <= 1e-9;
}

[[nodiscard]] auto has_duplicate_consecutive_point(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (same_point(points[index], points[(index + 1) % points.size()]))
            return true;
    }
    return false;
}

[[nodiscard]] auto has_adjacent_overlap(const std::vector<ir::Point2>& points) -> bool {
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& previous = points[(index + points.size() - 1) % points.size()];
        const auto& current = points[index];
        const auto& next = points[(index + 1) % points.size()];
        if (std::abs(orientation(previous, current, next)) > 1e-9)
            continue;
        const double first_x = previous.x_mm - current.x_mm;
        const double first_y = previous.y_mm - current.y_mm;
        const double second_x = next.x_mm - current.x_mm;
        const double second_y = next.y_mm - current.y_mm;
        if (first_x * second_x + first_y * second_y > 1e-9)
            return true;
    }
    return false;
}

[[nodiscard]] auto lower_point(const ast::Point2Decl& point, SemanticResult& result) -> ir::Point2 {
    const auto x = lower_quantity(point.x, units::Dimension::length, false, result);
    const auto y = lower_quantity(point.y, units::Dimension::length, false, result);
    return {x.value, y.value};
}

[[nodiscard]] auto line_segment(const ir::Point2& start, const ir::Point2& end)
    -> ir::ProfileSegment {
    return {ir::ProfileSegmentKind::line, start, end, {}, 0.0, 0.0};
}

[[nodiscard]] auto tessellate(const std::vector<ir::ProfileSegment>& segments)
    -> std::vector<ir::Point2> {
    constexpr double full_circle = 2.0 * std::numbers::pi;
    constexpr double angular_step = full_circle / 32.0;
    std::vector<ir::Point2> points;
    for (const auto& segment : segments) {
        if (points.empty()) {
            points.push_back(segment.start);
        }
        if (segment.kind == ir::ProfileSegmentKind::line) {
            points.push_back(segment.end);
            continue;
        }
        const double start_angle = std::atan2(segment.start.y_mm - segment.center.y_mm,
                                              segment.start.x_mm - segment.center.x_mm);
        const auto steps = static_cast<std::size_t>(
            std::max(1.0, std::ceil(std::abs(segment.sweep_radians) / angular_step)));
        for (std::size_t step = 1; step <= steps; ++step) {
            const double fraction = static_cast<double>(step) / static_cast<double>(steps);
            const double angle = start_angle + segment.sweep_radians * fraction;
            points.push_back({segment.center.x_mm + segment.radius_mm * std::cos(angle),
                              segment.center.y_mm + segment.radius_mm * std::sin(angle)});
        }
    }
    if (points.size() > 1 && same_point(points.front(), points.back())) {
        points.pop_back();
    }
    return points;
}

auto reverse_segments(std::vector<ir::ProfileSegment>& segments) -> void {
    std::vector<ir::ProfileSegment> reversed;
    reversed.reserve(segments.size());
    for (auto iterator = segments.rbegin(); iterator != segments.rend(); ++iterator) {
        auto segment = *iterator;
        std::swap(segment.start, segment.end);
        segment.sweep_radians = -segment.sweep_radians;
        reversed.push_back(std::move(segment));
    }
    segments = std::move(reversed);
}

auto lower_profile(const ast::ProfileDecl& profile, ir::Profile& lowered, SemanticResult& result)
    -> void {
    constexpr double full_circle = 2.0 * std::numbers::pi;
    if (profile.mode == ast::ProfileMode::points) {
        for (const auto& point : profile.points) {
            lowered.points.push_back(lower_point(point, result));
        }
        if (signed_area(lowered.points) < 0.0) {
            std::ranges::reverse(lowered.points);
        }
        for (std::size_t index = 0; index < lowered.points.size(); ++index) {
            lowered.segments.push_back(line_segment(
                lowered.points[index], lowered.points[(index + 1) % lowered.points.size()]));
        }
        return;
    }
    if (profile.mode == ast::ProfileMode::circle) {
        const auto center = lower_point(profile.circle_center, result);
        const auto radius =
            lower_quantity(profile.circle_radius, units::Dimension::length, true, result);
        const ir::Point2 start{center.x_mm + radius.value, center.y_mm};
        lowered.segments.push_back({ir::ProfileSegmentKind::circular_arc, start, start, center,
                                    radius.value, full_circle});
        lowered.points = tessellate(lowered.segments);
        return;
    }
    if (profile.mode != ast::ProfileMode::path) {
        return;
    }
    const auto start = lower_point(profile.path_start, result);
    auto current = start;
    for (const auto& source_segment : profile.path_segments) {
        const auto end = lower_point(source_segment.end, result);
        if (same_point(current, end)) {
            add_error(result, "ICAD-S0029", "profile segment endpoints must be different",
                      source_segment.location);
            continue;
        }
        if (source_segment.kind == ast::PathSegmentKind::line) {
            lowered.segments.push_back(line_segment(current, end));
            current = end;
            continue;
        }
        const auto center = lower_point(source_segment.center, result);
        const double start_radius =
            std::hypot(current.x_mm - center.x_mm, current.y_mm - center.y_mm);
        const double end_radius = std::hypot(end.x_mm - center.x_mm, end.y_mm - center.y_mm);
        const double scale = std::max({1.0, start_radius, end_radius});
        if (start_radius <= 1e-9 || std::abs(start_radius - end_radius) > 1e-7 * scale) {
            add_error(result, "ICAD-S0029",
                      "ARC start and end must have the same non-zero distance from CENTER",
                      source_segment.location);
        }
        const double start_angle =
            std::atan2(current.y_mm - center.y_mm, current.x_mm - center.x_mm);
        const double end_angle = std::atan2(end.y_mm - center.y_mm, end.x_mm - center.x_mm);
        double sweep = end_angle - start_angle;
        if (source_segment.counterclockwise) {
            while (sweep <= 0.0)
                sweep += full_circle;
        } else {
            while (sweep >= 0.0)
                sweep -= full_circle;
        }
        lowered.segments.push_back(
            {ir::ProfileSegmentKind::circular_arc, current, end, center, start_radius, sweep});
        current = end;
    }
    if (!same_point(current, start)) {
        lowered.segments.push_back(line_segment(current, start));
    }
    lowered.points = tessellate(lowered.segments);
    if (signed_area(lowered.points) < 0.0) {
        reverse_segments(lowered.segments);
        lowered.points = tessellate(lowered.segments);
    }
}

} // namespace

auto analyze(const ast::Program& program) -> SemanticResult {
    SemanticResult result;
    ir::Project lowered;
    lowered.name = program.project_name;

    const auto default_unit = units::find(program.default_length_unit);
    if (!default_unit || default_unit->dimension != units::Dimension::length) {
        add_error(result, "ICAD-S0005", "UNITS must name a supported length unit",
                  program.location);
    } else {
        lowered.canonical_length_unit =
            std::string{units::canonical_symbol(units::Dimension::length)};
    }

    std::unordered_map<std::string, ir::Quantity> parameter_values;
    for (const auto& parameter : program.parameters) {
        const auto unit = units::find(parameter.value.unit);
        const auto dimension = unit ? unit->dimension : units::Dimension::unknown;
        auto quantity = lower_quantity(parameter.value, dimension, false, result);
        parameter_values.emplace(parameter.name, quantity);
        lowered.parameters.push_back(ir::Parameter{parameter.name, std::move(quantity)});
    }
    if (program.tolerance.declared) {
        const auto linear = lower_value(program.tolerance.linear, units::Dimension::length,
                                        parameter_values, result);
        const auto angular = lower_value(program.tolerance.angular, units::Dimension::angle,
                                         parameter_values, result);
        if (linear.value <= 0.0 || linear.value > 1.0 || angular.value <= 0.0 ||
            angular.value > 1.0) {
            add_error(result, "ICAD-S0040",
                      "TOLERANCE values must be greater than zero and at most 1 mm/deg",
                      program.tolerance.location);
        }
        lowered.tolerance = {linear.value, angular.value};
    }

    std::unordered_set<std::string> spatial_names;
    for (const auto& angle : program.angles) {
        if (!spatial_names.insert(angle.name).second || parameter_values.contains(angle.name)) {
            add_error(result, "ICAD-S0031", "duplicate named value '" + angle.name + "'",
                      angle.location);
        }
        auto quantity = lower_value(angle.value, units::Dimension::angle, parameter_values, result);
        parameter_values[angle.name] = quantity;
        lowered.angles.push_back({angle.name, quantity.value});
    }

    std::unordered_map<std::string, std::array<double, 3>> point_values;
    for (const auto& point : program.points) {
        if (!spatial_names.insert(point.name).second || parameter_values.contains(point.name)) {
            add_error(result, "ICAD-S0031", "duplicate spatial name '" + point.name + "'",
                      point.location);
        }
    }
    for (const auto& vector : program.vectors) {
        if (!spatial_names.insert(vector.name).second || parameter_values.contains(vector.name)) {
            add_error(result, "ICAD-S0031", "duplicate spatial name '" + vector.name + "'",
                      vector.location);
        }
    }
    std::unordered_map<std::string, std::array<double, 3>> vector_values;
    std::unordered_map<std::string, double> point_distances;
    std::unordered_map<std::string, double> vector_angles;
    for (const auto& point : program.points) {
        if (point.derived)
            continue;
        std::array<double, 3> position{};
        for (std::size_t axis = 0; axis < position.size(); ++axis) {
            position[axis] = lower_value(point.coordinates[axis], units::Dimension::length,
                                         parameter_values, result)
                                 .value;
        }
        point_values.emplace(point.name, position);
    }
    for (const auto& vector : program.vectors) {
        if (vector.derived || vector.rotated)
            continue;
        const double magnitude =
            std::hypot(vector.components[0], vector.components[1], vector.components[2]);
        if (magnitude <= 1e-12) {
            add_error(result, "ICAD-S0031", "VECTOR magnitude must be greater than zero",
                      vector.location);
        }
        std::array<double, 3> direction{};
        if (magnitude > 1e-12) {
            for (std::size_t axis = 0; axis < direction.size(); ++axis) {
                direction[axis] = vector.components[axis] / magnitude;
            }
        }
        vector_values.emplace(vector.name, direction);
    }

    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& point : program.points) {
            if (!point.derived || point_values.contains(point.name))
                continue;
            const auto base = point_values.find(point.base_point);
            const auto direction = vector_values.find(point.direction);
            if (base == point_values.end() || direction == vector_values.end())
                continue;
            const double distance =
                lower_value(point.distance, units::Dimension::length, parameter_values, result)
                    .value;
            std::array<double, 3> position{};
            for (std::size_t axis = 0; axis < position.size(); ++axis)
                position[axis] = base->second[axis] + direction->second[axis] * distance;
            point_values.emplace(point.name, position);
            point_distances.emplace(point.name, distance);
            progress = true;
        }
        for (const auto& vector : program.vectors) {
            if ((!vector.derived && !vector.rotated) || vector_values.contains(vector.name))
                continue;
            std::array<double, 3> direction{};
            if (vector.rotated) {
                const auto source = vector_values.find(vector.source_vector);
                const auto axis = vector_values.find(vector.around_axis);
                if (source == vector_values.end() || axis == vector_values.end())
                    continue;
                const double degrees =
                    lower_value(vector.angle, units::Dimension::angle, parameter_values, result)
                        .value;
                const double radians = degrees * std::numbers::pi / 180.0;
                const double cosine = std::cos(radians);
                const double sine = std::sin(radians);
                const auto& value = source->second;
                const auto& rotation_axis = axis->second;
                const std::array<double, 3> cross{
                    rotation_axis[1] * value[2] - rotation_axis[2] * value[1],
                    rotation_axis[2] * value[0] - rotation_axis[0] * value[2],
                    rotation_axis[0] * value[1] - rotation_axis[1] * value[0]};
                const double projection = rotation_axis[0] * value[0] +
                                          rotation_axis[1] * value[1] +
                                          rotation_axis[2] * value[2];
                for (std::size_t component = 0; component < direction.size(); ++component) {
                    direction[component] = value[component] * cosine + cross[component] * sine +
                                           rotation_axis[component] * projection * (1.0 - cosine);
                }
                vector_angles.emplace(vector.name, degrees);
            } else {
                const auto from = point_values.find(vector.from_point);
                const auto to = point_values.find(vector.to_point);
                if (from == point_values.end() || to == point_values.end())
                    continue;
                for (std::size_t axis = 0; axis < direction.size(); ++axis)
                    direction[axis] = to->second[axis] - from->second[axis];
            }
            const double magnitude = std::hypot(direction[0], direction[1], direction[2]);
            if (magnitude <= 1e-12) {
                add_error(result, "ICAD-S0031",
                          vector.rotated ? "rotated VECTOR magnitude must be greater than zero"
                                         : "derived VECTOR endpoints must be different",
                          vector.location);
            } else {
                for (auto& component : direction)
                    component /= magnitude;
            }
            vector_values.emplace(vector.name, direction);
            progress = true;
        }
    }

    for (const auto& point : program.points) {
        const auto value = point_values.find(point.name);
        if (value == point_values.end()) {
            add_error(result, "ICAD-S0031",
                      "derived POINT3 has an unknown or cyclic point/vector dependency",
                      point.location);
            continue;
        }
        ir::SpatialPoint lowered_point;
        lowered_point.name = point.name;
        lowered_point.position_mm = value->second;
        if (point.derived) {
            lowered_point.kind = ir::SpatialPointKind::offset;
            lowered_point.base_point = point.base_point;
            lowered_point.direction = point.direction;
            lowered_point.distance_mm = point_distances.at(point.name);
            lowered_point.distance_reference = point.distance.parameter_reference;
        }
        lowered.points.push_back(std::move(lowered_point));
    }
    for (const auto& vector : program.vectors) {
        const auto value = vector_values.find(vector.name);
        if (value == vector_values.end()) {
            add_error(result, "ICAD-S0031",
                      "derived VECTOR has an unknown or cyclic point/vector dependency",
                      vector.location);
            continue;
        }
        ir::Direction lowered_vector;
        lowered_vector.name = vector.name;
        lowered_vector.unit = value->second;
        if (vector.rotated) {
            lowered_vector.kind = ir::DirectionKind::rotated;
            lowered_vector.source_direction = vector.source_vector;
            lowered_vector.around_axis = vector.around_axis;
            lowered_vector.angle_degrees = vector_angles.at(vector.name);
            lowered_vector.angle_reference = vector.angle.parameter_reference;
        } else if (vector.derived) {
            lowered_vector.kind = ir::DirectionKind::between_points;
            lowered_vector.from_point = vector.from_point;
            lowered_vector.to_point = vector.to_point;
        }
        lowered.vectors.push_back(std::move(lowered_vector));
    }

    std::unordered_set<std::string> material_names;
    for (const auto& material : program.materials) {
        material_names.insert(material.name);
        if (material.preset.empty()) {
            add_error(result, "ICAD-S0011", "material block requires PRESET", material.location);
            continue;
        }
        const auto preset = materials::find(material.preset);
        if (!preset) {
            add_error(result, "ICAD-S0011", "unknown predefined material '" + material.preset + "'",
                      material.location);
            continue;
        }
        ir::Material lowered_material{material.name, material.preset, preset->base_color,
                                      preset->metallic, preset->roughness,
                                      std::string{preset->texture}, preset->texture_seed};
        if (material.has_base_color) {
            if (std::ranges::any_of(material.base_color,
                                    [](double value) { return value < 0.0 || value > 1.0; })) {
                add_error(result, "ICAD-S0011", "BASE_COLOR components must be within [0, 1]",
                          material.location);
            }
            lowered_material.base_color = material.base_color;
        }
        if (material.has_metallic) {
            if (material.metallic < 0.0 || material.metallic > 1.0) {
                add_error(result, "ICAD-S0011", "METALLIC must be within [0, 1]",
                          material.location);
            }
            lowered_material.metallic = material.metallic;
        }
        if (material.has_roughness) {
            if (material.roughness < 0.0 || material.roughness > 1.0) {
                add_error(result, "ICAD-S0011", "ROUGHNESS must be within [0, 1]",
                          material.location);
            }
            lowered_material.roughness = material.roughness;
        }
        if (material.has_texture_scale) {
            const auto scale = lower_value(material.texture_scale, units::Dimension::length,
                                           parameter_values, result);
            if (scale.value <= 0.0) {
                add_error(result, "ICAD-S0011", "TEXTURE_SCALE must be positive",
                          material.location);
            }
            lowered_material.texture_scale_mm = scale.value;
        }
        if (!material.uv_mode.empty()) {
            constexpr std::string_view uv_modes[]{"BOX", "PLANAR", "CYLINDRICAL", "SPHERICAL"};
            if (std::ranges::find(uv_modes, material.uv_mode) == std::end(uv_modes)) {
                add_error(result, "ICAD-S0011",
                          "UV_MODE must be BOX, PLANAR, CYLINDRICAL, or SPHERICAL",
                          material.location);
            }
            lowered_material.uv_mode = material.uv_mode;
        }
        lowered.materials.push_back(std::move(lowered_material));
    }

    std::unordered_map<std::string, std::size_t> profile_indices;
    for (const auto& profile : program.profiles) {
        ir::Profile lowered_profile;
        lowered_profile.name = profile.name;
        lower_profile(profile, lowered_profile, result);
        if (lowered_profile.points.size() < 3) {
            add_error(result, "ICAD-S0019", "profile requires at least three points",
                      profile.location);
        } else if (has_duplicate_consecutive_point(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile must not contain duplicate consecutive points",
                      profile.location);
        } else if (has_adjacent_overlap(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile boundary must not backtrack or overlap",
                      profile.location);
        } else if (std::abs(signed_area(lowered_profile.points)) <= 1e-9) {
            add_error(result, "ICAD-S0020", "profile area must be non-zero", profile.location);
        } else if (self_intersects(lowered_profile.points)) {
            add_error(result, "ICAD-S0020", "profile must not self-intersect", profile.location);
        }
        profile_indices.emplace(profile.name, lowered.profiles.size());
        lowered.profiles.push_back(std::move(lowered_profile));
    }

    for (const auto& sketch : program.sketches) {
        ir::Sketch lowered_sketch;
        lowered_sketch.name = sketch.name;
        std::unordered_set<std::string> sketch_point_names;
        bool valid = true;
        for (const auto& point : sketch.points) {
            if (!sketch_point_names.insert(point.name).second) {
                valid = false;
                continue;
            }
            const double x = lower_value(point.x, units::Dimension::length, parameter_values,
                                         result)
                                 .value;
            const double y = lower_value(point.y, units::Dimension::length, parameter_values,
                                         result)
                                 .value;
            lowered_sketch.points.push_back(
                ir::SketchPoint{point.name, {x, y}, {x, y}, point.fixed});
        }
        if (sketch.points.size() < 2) {
            add_error(result, "ICAD-S0037", "SKETCH requires at least two named points",
                      sketch.location);
            valid = false;
        }
        for (const auto& constraint : sketch.constraints) {
            const bool angle = constraint.kind == "ANGLE";
            const bool dimensional = constraint.kind == "DISTANCE" || angle;
            const bool supported = dimensional || constraint.kind == "HORIZONTAL" ||
                                   constraint.kind == "VERTICAL" ||
                                   constraint.kind == "COINCIDENT";
            if (!supported) {
                add_error(result, "ICAD-S0037",
                          "unsupported sketch constraint kind '" + constraint.kind + "'",
                          constraint.location);
                valid = false;
            }
            const std::size_t expected_references = angle ? 3 : 2;
            if (constraint.references.size() != expected_references ||
                std::ranges::any_of(constraint.references, [&](const auto& reference) {
                    return !sketch_point_names.contains(reference);
                })) {
                add_error(result, "ICAD-S0037",
                          "sketch constraint references unknown or invalid points",
                          constraint.location);
                valid = false;
            }
            if (constraint.references.size() >= 2 &&
                constraint.references[0] == constraint.references[1]) {
                add_error(result, "ICAD-S0037",
                          "sketch constraint requires distinct point references",
                          constraint.location);
                valid = false;
            }
            ir::Quantity target;
            if (dimensional) {
                target = lower_value(constraint.target,
                                     angle ? units::Dimension::angle : units::Dimension::length,
                                     parameter_values, result);
                if (target.value < 0.0 || (angle && target.value > 180.0)) {
                    add_error(result, "ICAD-S0037",
                              "sketch distance or angle target is outside its valid range",
                              constraint.location);
                    valid = false;
                }
            }
            lowered_sketch.constraints.push_back(
                ir::SketchConstraint{constraint.name, constraint.kind, constraint.references,
                                     target.value, target.unit,
                                     constraint.target.parameter_reference});
        }
        if (valid) {
            constraints::solve_sketch(lowered_sketch);
            if (lowered_sketch.status == ir::SketchSolveStatus::inconsistent) {
                add_error(result, "ICAD-S0038",
                          "SKETCH constraints are inconsistent or failed to converge",
                          sketch.location);
            }
        }
        if (valid && lowered_sketch.status != ir::SketchSolveStatus::inconsistent &&
            lowered_sketch.points.size() >= 3) {
            ir::Profile sketch_profile;
            sketch_profile.name = sketch.name;
            for (const auto& point : lowered_sketch.points)
                sketch_profile.points.push_back(point.solved);
            if (signed_area(sketch_profile.points) < 0.0)
                std::ranges::reverse(sketch_profile.points);
            if (std::abs(signed_area(sketch_profile.points)) <= 1e-9 ||
                has_duplicate_consecutive_point(sketch_profile.points) ||
                has_adjacent_overlap(sketch_profile.points) ||
                self_intersects(sketch_profile.points)) {
                add_error(result, "ICAD-S0039",
                          "solved SKETCH boundary must be a simple non-zero closed profile",
                          sketch.location);
            } else {
                for (std::size_t point = 0; point < sketch_profile.points.size(); ++point) {
                    const auto& start = sketch_profile.points[point];
                    const auto& end = sketch_profile.points[(point + 1) % sketch_profile.points.size()];
                    sketch_profile.segments.push_back(ir::ProfileSegment{
                        ir::ProfileSegmentKind::line, start, end, {}, 0.0, 0.0});
                }
                profile_indices.emplace(sketch.name, lowered.profiles.size());
                lowered.profiles.push_back(std::move(sketch_profile));
            }
        }
        lowered.sketches.push_back(std::move(lowered_sketch));
    }

    std::unordered_set<std::string> body_names;
    for (const auto& body : program.bodies) {
        body_names.insert(body.name);
    }

    std::unordered_set<std::string> posed_bodies;
    for (const auto& pose : program.poses) {
        if (!body_names.contains(pose.body)) {
            add_error(result, "ICAD-S0032", "POSE references unknown BODY '" + pose.body + "'",
                      pose.location);
        }
        const auto point = point_values.find(pose.point);
        if (point == point_values.end()) {
            add_error(result, "ICAD-S0032", "POSE references unknown POINT3 '" + pose.point + "'",
                      pose.location);
        }
        if (!posed_bodies.insert(pose.body).second) {
            add_error(result, "ICAD-S0032", "BODY may have only one POSE", pose.location);
        }
        ir::Transform transform;
        if (point != point_values.end()) {
            transform.position_mm = point->second;
        }
        for (std::size_t axis = 0; axis < transform.rotation_deg.size(); ++axis) {
            transform.rotation_deg[axis] =
                lower_value(pose.rotation[axis], units::Dimension::angle, parameter_values, result)
                    .value;
        }
        lowered.poses.push_back({pose.body, pose.point, transform});
    }

    std::unordered_set<std::string> occurrence_names = body_names;
    for (const auto& instance : program.instances) {
        if (!body_names.contains(instance.body)) {
            add_error(result, "ICAD-S0041",
                      "INSTANCE references unknown BODY definition '" + instance.body + "'",
                      instance.location);
        }
        const auto point = point_values.find(instance.point);
        if (point == point_values.end()) {
            add_error(result, "ICAD-S0041",
                      "INSTANCE references unknown POINT3 '" + instance.point + "'",
                      instance.location);
        }
        if (!occurrence_names.insert(instance.name).second) {
            add_error(result, "ICAD-S0041", "INSTANCE name collides with an occurrence",
                      instance.location);
        }
        ir::Transform transform;
        if (point != point_values.end())
            transform.position_mm = point->second;
        for (std::size_t axis = 0; axis < transform.rotation_deg.size(); ++axis) {
            transform.rotation_deg[axis] =
                lower_value(instance.rotation[axis], units::Dimension::angle, parameter_values,
                            result)
                    .value;
        }
        lowered.instances.push_back(
            {instance.name, instance.body, instance.point, transform});
    }

    for (const auto& body : program.bodies) {
        ir::Body lowered_body;
        lowered_body.name = body.name;
        lowered_body.material = body.material;
        if (!body.material.empty() && !material_names.contains(body.material)) {
            add_error(result, "ICAD-S0012",
                      "BODY references unknown material '" + body.material + "'", body.location);
        }
        for (std::size_t feature_index = 0; feature_index < body.features.size(); ++feature_index) {
            const auto& feature = body.features[feature_index];
            ir::Feature lowered_feature;
            lowered_feature.name = feature.name;
            lowered_feature.type = feature.type;
            lowered_feature.profile = feature.profile;
            lowered_feature.target_profile = feature.target_profile;
            lowered_feature.selected_edge_point = feature.selected_edge_point;
            lowered_feature.direction = feature.direction;
            lowered_feature.plane_point = feature.plane_point;
            lowered_feature.plane_normal = feature.plane_normal;
            lowered_feature.path_points = feature.path_points;
            lowered_feature.count = feature.count;
            if (feature.operation.empty() || feature.operation == "NEW") {
                lowered_feature.operation = ir::FeatureOperation::create;
            } else if (feature.operation == "UNION") {
                lowered_feature.operation = ir::FeatureOperation::unite;
            } else if (feature.operation == "CUT") {
                lowered_feature.operation = ir::FeatureOperation::cut;
            } else if (feature.operation == "INTERSECT") {
                lowered_feature.operation = ir::FeatureOperation::intersect;
            } else {
                add_error(result, "ICAD-S0034",
                          "OPERATION must be NEW, UNION, CUT, or INTERSECT", feature.location);
            }
            if (feature_index == 0 &&
                lowered_feature.operation != ir::FeatureOperation::create) {
                add_error(result, "ICAD-S0034",
                          "the first feature in a BODY must use OPERATION NEW", feature.location);
            }

            if (feature.type.empty()) {
                add_error(result, "ICAD-S0006", "feature must declare TYPE", feature.location);
                lowered_body.features.push_back(std::move(lowered_feature));
                continue;
            }
            const auto* schema = types::find_feature_schema(feature.type);
            if (schema == nullptr) {
                add_error(result, "ICAD-S0007", "unsupported feature TYPE '" + feature.type + "'",
                          feature.location);
                lowered_body.features.push_back(std::move(lowered_feature));
                continue;
            }

            const bool edge_modifier = feature.type == "CHAMFER" || feature.type == "FILLET";
            const bool pattern_modifier = feature.type == "LINEAR_PATTERN";
            const bool mirror_modifier = feature.type == "MIRROR";
            const bool sweep_feature = feature.type == "SWEEP";
            const bool loft_feature = feature.type == "LOFT";
            const bool freeform_feature = feature.type == "FREEFORM";
            const bool modifier = edge_modifier || pattern_modifier || mirror_modifier;
            if (modifier && feature_index == 0) {
                add_error(result, "ICAD-S0035",
                          feature.type + " requires an earlier solid feature in the BODY",
                          feature.location);
            }
            if (modifier && !feature.operation.empty()) {
                add_error(result, "ICAD-S0035",
                          "modeling modifiers do not accept OPERATION", feature.location);
            }
            if (edge_modifier) {
                if (feature.selected_edge_point.empty()) {
                    add_error(result, "ICAD-S0035",
                              feature.type + " requires SELECT EDGE NEAREST point",
                              feature.location);
                } else if (!point_values.contains(feature.selected_edge_point)) {
                    add_error(result, "ICAD-S0035",
                              "edge selector references unknown POINT3 '" +
                                  feature.selected_edge_point + "'",
                              feature.location);
                }
            } else if (pattern_modifier) {
                if (feature.direction.empty() || !vector_values.contains(feature.direction)) {
                    add_error(result, "ICAD-S0035",
                              "LINEAR_PATTERN requires DIRECTION with a known VECTOR",
                              feature.location);
                }
                if (!feature.has_count || feature.count < 2 || feature.count > 1000) {
                    add_error(result, "ICAD-S0035",
                              "LINEAR_PATTERN COUNT must be between 2 and 1000",
                              feature.location);
                }
            } else if (mirror_modifier) {
                if (feature.plane_point.empty() || !point_values.contains(feature.plane_point) ||
                    feature.plane_normal.empty() ||
                    !vector_values.contains(feature.plane_normal)) {
                    add_error(result, "ICAD-S0035",
                              "MIRROR requires PLANE with a known POINT3 and normal VECTOR",
                              feature.location);
                }
            } else if (sweep_feature) {
                if (feature.path_points.size() < 2) {
                    add_error(result, "ICAD-S0036",
                              "SWEEP requires PATH with at least two POINT3 values",
                              feature.location);
                }
                for (std::size_t point = 0; point < feature.path_points.size(); ++point) {
                    if (!point_values.contains(feature.path_points[point])) {
                        add_error(result, "ICAD-S0036",
                                  "SWEEP PATH references unknown POINT3 '" +
                                      feature.path_points[point] + "'",
                                  feature.location);
                        continue;
                    }
                    if (point != 0 && point_values.contains(feature.path_points[point - 1])) {
                        const auto& first = point_values.at(feature.path_points[point - 1]);
                        const auto& second = point_values.at(feature.path_points[point]);
                        const double squared =
                            (first[0] - second[0]) * (first[0] - second[0]) +
                            (first[1] - second[1]) * (first[1] - second[1]) +
                            (first[2] - second[2]) * (first[2] - second[2]);
                        if (squared <= 1e-18)
                            add_error(result, "ICAD-S0036",
                                      "SWEEP PATH cannot repeat consecutive POINT3 values",
                                      feature.location);
                    }
                }
            } else if (loft_feature || freeform_feature) {
                if (feature.target_profile.empty() ||
                    !profile_indices.contains(feature.target_profile)) {
                    add_error(result, "ICAD-S0036",
                              feature.type + " requires a known TARGET_PROFILE",
                              feature.location);
                }
                if (freeform_feature &&
                    (!feature.has_count || feature.count < 3 || feature.count > 128)) {
                    add_error(result, "ICAD-S0036",
                              "FREEFORM COUNT must be between 3 and 128 sections",
                              feature.location);
                }
            } else if (!feature.selected_edge_point.empty() || !feature.direction.empty() ||
                       !feature.plane_point.empty() || feature.has_count ||
                       !feature.path_points.empty() || !feature.target_profile.empty()) {
                add_error(result, "ICAD-S0035",
                          "selection, direction, plane, and count are valid only for modeling "
                          "modifiers",
                          feature.location);
            }

            std::unordered_set<std::string> seen_properties;
            for (const auto& property : feature.properties) {
                if (!seen_properties.insert(property.name).second) {
                    add_error(result, "ICAD-S0008", "duplicate property '" + property.name + "'",
                              property.location);
                    continue;
                }
                const auto* property_spec = types::find_property(*schema, property.name);
                if (property_spec == nullptr) {
                    add_error(result, "ICAD-S0009",
                              "property '" + property.name + "' is not valid for TYPE " +
                                  feature.type,
                              property.location);
                    continue;
                }
                ir::Quantity quantity;
                if (!property.parameter_reference.empty()) {
                    const auto parameter = parameter_values.find(property.parameter_reference);
                    if (parameter == parameter_values.end()) {
                        add_error(result, "ICAD-S0022",
                                  "unknown parameter '" + property.parameter_reference + "'",
                                  property.location);
                        continue;
                    }
                    quantity = parameter->second;
                    if (quantity.dimension != property_spec->dimension) {
                        add_error(result, "ICAD-S0023",
                                  "parameter '" + property.parameter_reference +
                                      "' has the wrong physical dimension for " + property.name,
                                  property.location);
                    }
                    if (property_spec->must_be_positive && quantity.value <= 0.0) {
                        add_error(result, "ICAD-S0003", "quantity must be greater than zero",
                                  property.location);
                    }
                } else {
                    quantity = lower_quantity(property.value, property_spec->dimension,
                                              property_spec->must_be_positive, result);
                }
                lowered_feature.properties.push_back(
                    ir::Property{property.name, std::move(quantity)});
            }
            for (const auto& property_spec : schema->properties) {
                if (property_spec.required &&
                    !seen_properties.contains(std::string{property_spec.name})) {
                    add_error(result, "ICAD-S0010",
                              "missing required property '" + std::string{property_spec.name} + "'",
                              feature.location);
                }
            }
            const bool profile_feature = feature.type == "EXTRUDE" || feature.type == "REVOLVE" ||
                                         sweep_feature || loft_feature || freeform_feature;
            if (profile_feature && feature.profile.empty()) {
                add_error(result, "ICAD-S0021", feature.type + " requires PROFILE",
                          feature.location);
            } else if (!feature.profile.empty() && !profile_indices.contains(feature.profile)) {
                add_error(result, "ICAD-S0021", "unknown profile '" + feature.profile + "'",
                          feature.location);
            } else if (!profile_feature && !feature.profile.empty()) {
                add_error(result, "ICAD-S0024", "PROFILE is not valid for this feature TYPE",
                          feature.location);
            }
            if (feature.type == "REVOLVE" && profile_indices.contains(feature.profile)) {
                const auto& profile = lowered.profiles[profile_indices.at(feature.profile)];
                if (std::ranges::any_of(profile.points,
                                        [](const auto& point) { return point.x_mm <= 0.0; })) {
                    add_error(result, "ICAD-S0025",
                              "REVOLVE profile radius coordinates must be greater than zero",
                              feature.location);
                }
                const auto angle =
                    std::ranges::find(lowered_feature.properties, "ANGLE", &ir::Property::name);
                if (angle != lowered_feature.properties.end() &&
                    std::abs(angle->value.value - 360.0) > 1e-9) {
                    add_error(result, "ICAD-S0025", "REVOLVE currently requires ANGLE 360 deg",
                              feature.location);
                }
            }
            lowered_body.features.push_back(std::move(lowered_feature));
        }
        lowered.bodies.push_back(std::move(lowered_body));
    }

    std::unordered_set<std::string> joint_names;
    std::unordered_map<std::string, std::string> joint_parent;
    for (const auto& joint : program.joints) {
        if (!joint_names.insert(joint.name).second) {
            add_error(result, "ICAD-S0033", "duplicate JOINT name '" + joint.name + "'",
                      joint.location);
        }
        const bool parent_exists =
            joint.parent_body == "WORLD" || occurrence_names.contains(joint.parent_body);
        if (!parent_exists || !occurrence_names.contains(joint.child_body) ||
            joint.parent_body == joint.child_body) {
            add_error(result, "ICAD-S0033",
                      "JOINT must connect WORLD or one occurrence to a different existing child occurrence",
                      joint.location);
        }
        if (!point_values.contains(joint.point) || !vector_values.contains(joint.axis)) {
            add_error(result, "ICAD-S0033", "JOINT requires an existing POINT3 and VECTOR axis",
                      joint.location);
        }
        if (joint_parent.contains(joint.child_body)) {
            add_error(result, "ICAD-S0033", "a mechanism BODY may have only one parent JOINT",
                      joint.location);
        } else {
            joint_parent.emplace(joint.child_body, joint.parent_body);
        }
        ir::Joint lowered_joint;
        lowered_joint.name = joint.name;
        lowered_joint.parent_body = joint.parent_body;
        lowered_joint.child_body = joint.child_body;
        lowered_joint.point = joint.point;
        lowered_joint.axis = joint.axis;
        if (joint.kind == "FIXED") {
            lowered_joint.kind = ir::JointKind::fixed;
        } else {
            const auto dimension =
                joint.kind == "REVOLUTE" ? units::Dimension::angle : units::Dimension::length;
            if (joint.kind != "REVOLUTE" && joint.kind != "PRISMATIC") {
                add_error(result, "ICAD-S0033", "JOINT type must be FIXED, REVOLUTE, or PRISMATIC",
                          joint.location);
            }
            lowered_joint.kind =
                joint.kind == "PRISMATIC" ? ir::JointKind::prismatic : ir::JointKind::revolute;
            const auto value = lower_value(joint.value, dimension, parameter_values, result);
            const auto lower = lower_value(joint.lower_limit, dimension, parameter_values, result);
            const auto upper = lower_value(joint.upper_limit, dimension, parameter_values, result);
            lowered_joint.value = value.value;
            lowered_joint.lower_limit = lower.value;
            lowered_joint.upper_limit = upper.value;
            lowered_joint.unit = value.unit;
            lowered_joint.value_reference = joint.value.parameter_reference;
            lowered_joint.lower_limit_reference = joint.lower_limit.parameter_reference;
            lowered_joint.upper_limit_reference = joint.upper_limit.parameter_reference;
            lowered_joint.driven = joint.driven;
            if (lower.value > upper.value || value.value < lower.value - 1e-9 ||
                value.value > upper.value + 1e-9) {
                add_error(result, "ICAD-S0033", "JOINT VALUE must be within ordered LIMIT values",
                          joint.location);
            }
        }
        lowered.joints.push_back(std::move(lowered_joint));
    }
    for (const auto& [child, parent] : joint_parent) {
        std::unordered_set<std::string> ancestors{child};
        auto current = parent;
        while (current != "WORLD" && joint_parent.contains(current)) {
            if (!ancestors.insert(current).second) {
                add_error(result, "ICAD-S0033", "JOINT graph must be acyclic", program.location);
                break;
            }
            current = joint_parent.at(current);
        }
    }

    for (const auto& constraint : program.constraints) {
        const bool distance = constraint.kind == "MIN_DISTANCE";
        const bool coincident = constraint.kind == "COINCIDENT";
        const bool directional = constraint.kind == "PARALLEL" ||
                                 constraint.kind == "PERPENDICULAR" ||
                                 constraint.kind == "ANGLE_BETWEEN";
        if (!distance && !coincident && !directional) {
            add_error(result, "ICAD-S0026", "unsupported constraint kind '" + constraint.kind + "'",
                      constraint.location);
        }
        if (distance && (!body_names.contains(constraint.first_body) ||
                         !body_names.contains(constraint.second_body) ||
                         constraint.first_body == constraint.second_body)) {
            add_error(result, "ICAD-S0027",
                      "constraint must reference two different existing bodies",
                      constraint.location);
        }
        if (coincident && (!point_values.contains(constraint.first_body) ||
                           !point_values.contains(constraint.second_body))) {
            add_error(result, "ICAD-S0027", "COINCIDENT requires two POINT3 references",
                      constraint.location);
        }
        if (directional && (!vector_values.contains(constraint.first_body) ||
                            !vector_values.contains(constraint.second_body))) {
            add_error(result, "ICAD-S0027", "direction constraint requires two VECTOR references",
                      constraint.location);
        }
        ir::Quantity target;
        if (distance || coincident) {
            target = lower_value(constraint.target, units::Dimension::length, parameter_values,
                                 result);
        } else if (constraint.kind == "ANGLE_BETWEEN") {
            target =
                lower_value(constraint.target, units::Dimension::angle, parameter_values, result);
        } else {
            target = ir::Quantity{0.0, "deg", units::Dimension::angle};
        }
        if (target.value < 0.0 || (constraint.kind == "ANGLE_BETWEEN" && target.value > 180.0)) {
            add_error(result, "ICAD-S0028", "minimum distance cannot be negative",
                      constraint.location);
        }
        lowered.constraints.push_back(ir::Constraint{constraint.name, constraint.kind,
                                                     constraint.first_body, constraint.second_body,
                                                     target.value, target.value, target.unit,
                                                     constraint.target.parameter_reference});
    }

    constexpr std::string_view face_selectors[]{"X_MIN", "X_MAX", "Y_MIN", "Y_MAX",
                                                 "Z_MIN", "Z_MAX"};
    constexpr std::string_view edge_selectors[]{
        "X_AT_Y_MIN_Z_MIN", "X_AT_Y_MIN_Z_MAX", "X_AT_Y_MAX_Z_MIN",
        "X_AT_Y_MAX_Z_MAX", "Y_AT_X_MIN_Z_MIN", "Y_AT_X_MIN_Z_MAX",
        "Y_AT_X_MAX_Z_MIN", "Y_AT_X_MAX_Z_MAX", "Z_AT_X_MIN_Y_MIN",
        "Z_AT_X_MIN_Y_MAX", "Z_AT_X_MAX_Y_MIN", "Z_AT_X_MAX_Y_MAX"};
    std::unordered_set<std::string> mate_names;
    for (const auto& mate : program.mates) {
        if (!mate_names.insert(mate.name).second) {
            add_error(result, "ICAD-S0034", "duplicate MATE name '" + mate.name + "'",
                      mate.location);
        }
        if (!occurrence_names.contains(mate.first_occurrence) ||
            !occurrence_names.contains(mate.second_occurrence) ||
            mate.first_occurrence == mate.second_occurrence) {
            add_error(result, "ICAD-S0034",
                      "MATE must reference two different existing body or instance occurrences",
                      mate.location);
        }
        const bool face = mate.kind == "FACE";
        const bool edge = mate.kind == "EDGE";
        const auto selector_valid = [&](const std::string& selector) {
            return face ? std::ranges::contains(face_selectors, selector)
                        : std::ranges::contains(edge_selectors, selector);
        };
        if ((!face && !edge) || !selector_valid(mate.first_selector) ||
            !selector_valid(mate.second_selector)) {
            add_error(result, "ICAD-S0034",
                      "MATE requires a supported world-semantic FACE or EDGE selector",
                      mate.location);
        } else if (mate.first_selector.front() != mate.second_selector.front()) {
            add_error(result, "ICAD-S0034", "mated selectors must use the same world axis",
                      mate.location);
        }
        const auto target =
            lower_value(mate.target, units::Dimension::length, parameter_values, result);
        if (target.value < 0.0) {
            add_error(result, "ICAD-S0034", "MATE target cannot be negative", mate.location);
        }
        lowered.mates.push_back(
            ir::Mate{mate.name, face ? ir::MateKind::face : ir::MateKind::edge,
                     mate.first_occurrence, mate.first_selector, mate.second_occurrence,
                     mate.second_selector, target.value, mate.target.parameter_reference});
    }

    constexpr std::string_view backgrounds[]{"SKY_DAY", "STUDIO", "NIGHT", "TRANSPARENT"};
    for (const auto& scene : program.scenes) {
        ir::Scene lowered_scene;
        lowered_scene.name = scene.name;
        lowered_scene.background = scene.background;
        lowered_scene.duration_seconds =
            lower_quantity(scene.duration, units::Dimension::time, true, result).value;
        lowered_scene.frames_per_second = scene.frames_per_second;
        lowered_scene.loop_count = scene.loop_count;
        if (scene.frames_per_second <= 0.0 || scene.frames_per_second > 240.0) {
            add_error(result, "ICAD-S0013", "scene FPS must be greater than 0 and at most 240",
                      scene.location);
        }
        if (std::ranges::find(backgrounds, scene.background) == std::end(backgrounds)) {
            add_error(result, "ICAD-S0014", "unknown scene background '" + scene.background + "'",
                      scene.location);
        }
        for (const auto& light : scene.lights) {
            if (light.kind != "DIRECTIONAL" && light.kind != "POINT") {
                add_error(result, "ICAD-S0014", "LIGHT kind must be DIRECTIONAL or POINT",
                          light.location);
            }
            if (light.intensity < 0.0 || std::ranges::any_of(
                    light.color, [](double value) { return value < 0.0 || value > 1.0; })) {
                add_error(result, "ICAD-S0014",
                          "LIGHT intensity must be non-negative and color within [0, 1]",
                          light.location);
            }
            ir::SceneLight lowered_light;
            lowered_light.name = light.name;
            lowered_light.kind = light.kind;
            lowered_light.color = light.color;
            lowered_light.intensity = light.intensity;
            if (light.kind == "POINT") {
                const auto point = std::ranges::find(lowered.points, light.point,
                                                     &ir::SpatialPoint::name);
                if (light.point.empty() || point == lowered.points.end()) {
                    add_error(result, "ICAD-S0014", "POINT LIGHT requires AT with a named POINT3",
                              light.location);
                } else {
                    lowered_light.position_mm = point->position_mm;
                }
            } else if (!light.point.empty()) {
                add_error(result, "ICAD-S0014", "DIRECTIONAL LIGHT does not accept AT",
                          light.location);
            }
            lowered_scene.lights.push_back(std::move(lowered_light));
        }
        for (const auto& event : scene.events) {
            const auto time = lower_quantity(event.time, units::Dimension::time, false, result).value;
            if (time < 0.0 || time > lowered_scene.duration_seconds) {
                add_error(result, "ICAD-S0014", "EVENT time must be within scene duration",
                          event.location);
            }
            lowered_scene.events.push_back(ir::SceneEvent{time, event.name});
        }
        for (const auto& track : scene.tracks) {
            ir::AnimationTrack lowered_track;
            lowered_track.name = track.name;
            lowered_track.target_kind = track.target_kind;
            lowered_track.target = track.target;
            lowered_track.easing = track.easing;
            constexpr std::string_view easing_modes[]{"LINEAR", "EASE_IN", "EASE_OUT",
                                                       "EASE_IN_OUT", "STEP"};
            if (std::ranges::find(easing_modes, track.easing) == std::end(easing_modes)) {
                add_error(result, "ICAD-S0015",
                          "EASING must be LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT, or STEP",
                          track.location);
            }
            if (track.target_kind != "BODY" && track.target_kind != "CAMERA" &&
                track.target_kind != "JOINT" && track.target_kind != "VISIBILITY") {
                add_error(result, "ICAD-S0015",
                          "track target kind must be BODY, CAMERA, JOINT, or VISIBILITY",
                          track.location);
            } else if ((track.target_kind == "BODY" || track.target_kind == "VISIBILITY") &&
                       !occurrence_names.contains(track.target)) {
                add_error(result, "ICAD-S0016",
                          "track references unknown occurrence '" + track.target + "'", track.location);
            } else if (track.target_kind == "JOINT" && !joint_names.contains(track.target)) {
                add_error(result, "ICAD-S0016",
                          "track references unknown JOINT '" + track.target + "'", track.location);
            }
            if (track.keyframes.size() < 2) {
                add_error(result, "ICAD-S0017", "animation track requires at least two keyframes",
                          track.location);
            }
            double previous_time = -1.0;
            const auto target_joint = std::ranges::find(lowered.joints, track.target,
                                                        &ir::Joint::name);
            if (track.target_kind == "JOINT" && target_joint != lowered.joints.end() &&
                std::ranges::none_of(lowered.instances, [&](const auto& instance) {
                    return instance.name == target_joint->child_body;
                })) {
                add_error(result, "ICAD-S0019",
                          "JOINT animation currently requires an INSTANCE child occurrence",
                          track.location);
            }
            for (const auto& frame : track.keyframes) {
                const double time =
                    lower_quantity(frame.time, units::Dimension::time, false, result).value;
                if (time < 0.0 || time <= previous_time || time > lowered_scene.duration_seconds) {
                    add_error(result, "ICAD-S0018",
                              "keyframe times must increase within the scene duration",
                              frame.location);
                }
                previous_time = time;
                if (track.target_kind == "VISIBILITY") {
                    if (!frame.visibility_only) {
                        add_error(result, "ICAD-S0019",
                                  "VISIBILITY tracks require KEYFRAME TIME VISIBLE ON|OFF",
                                  frame.location);
                        continue;
                    }
                    ir::Keyframe lowered_frame;
                    lowered_frame.time_seconds = time;
                    lowered_frame.visible = frame.visible;
                    lowered_track.keyframes.push_back(std::move(lowered_frame));
                    continue;
                }
                if (track.target_kind == "JOINT") {
                    if (!frame.value_only) {
                        add_error(result, "ICAD-S0019",
                                  "JOINT tracks require KEYFRAME TIME VALUE scalar",
                                  frame.location);
                        continue;
                    }
                    const auto dimension =
                        target_joint != lowered.joints.end() &&
                                target_joint->kind == ir::JointKind::prismatic
                            ? units::Dimension::length
                            : units::Dimension::angle;
                    const auto value = lower_value(frame.joint_value, dimension, parameter_values,
                                                   result);
                    if (target_joint != lowered.joints.end()) {
                        if (target_joint->kind == ir::JointKind::fixed) {
                            add_error(result, "ICAD-S0019", "FIXED JOINT cannot be animated",
                                      frame.location);
                        } else if (value.value < target_joint->lower_limit - 1e-9 ||
                                   value.value > target_joint->upper_limit + 1e-9) {
                            add_error(result, "ICAD-S0019",
                                      "joint keyframe VALUE must remain within JOINT LIMIT",
                                      frame.location);
                        }
                    }
                    ir::Keyframe lowered_frame;
                    lowered_frame.time_seconds = time;
                    lowered_frame.joint_value = value.value;
                    lowered_frame.joint_unit = value.unit;
                    lowered_track.keyframes.push_back(std::move(lowered_frame));
                    continue;
                }
                if (frame.value_only || frame.visibility_only) {
                    add_error(result, "ICAD-S0019",
                              "BODY and CAMERA tracks require POSITION and ROTATION keyframes",
                              frame.location);
                    continue;
                }
                const auto px =
                    lower_quantity(frame.position_x, units::Dimension::length, false, result);
                const auto py =
                    lower_quantity(frame.position_y, units::Dimension::length, false, result);
                const auto pz =
                    lower_quantity(frame.position_z, units::Dimension::length, false, result);
                const auto rx =
                    lower_quantity(frame.rotation_x, units::Dimension::angle, false, result);
                const auto ry =
                    lower_quantity(frame.rotation_y, units::Dimension::angle, false, result);
                const auto rz =
                    lower_quantity(frame.rotation_z, units::Dimension::angle, false, result);
                ir::Keyframe lowered_frame;
                lowered_frame.time_seconds = time;
                lowered_frame.transform =
                    {{px.value, py.value, pz.value}, {rx.value, ry.value, rz.value}};
                lowered_track.keyframes.push_back(std::move(lowered_frame));
            }
            lowered_scene.tracks.push_back(std::move(lowered_track));
        }
        lowered.scenes.push_back(std::move(lowered_scene));
    }

    if (result.diagnostics.empty()) {
        result.project = std::move(lowered);
    }
    return result;
}

} // namespace icad::compiler
