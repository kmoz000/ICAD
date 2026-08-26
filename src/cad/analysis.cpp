#include "icad/cad/analysis.hpp"

#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>
#include <string_view>

namespace icad::cad {
namespace {

[[nodiscard]] auto empty_bounds() -> Bounds {
    const double high = std::numeric_limits<double>::max();
    const double low = std::numeric_limits<double>::lowest();
    return {{high, high, high}, {low, low, low}};
}

auto include(Bounds& bounds, const Point3& point) -> void {
    const double values[]{point.x, point.y, point.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds.minimum[axis] = std::min(bounds.minimum[axis], values[axis]);
        bounds.maximum[axis] = std::max(bounds.maximum[axis], values[axis]);
    }
}

auto include(Bounds& bounds, const Bounds& other) -> void {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds.minimum[axis] = std::min(bounds.minimum[axis], other.minimum[axis]);
        bounds.maximum[axis] = std::max(bounds.maximum[axis], other.maximum[axis]);
    }
}

[[nodiscard]] auto cross(const Point3& first, const Point3& second) -> Point3 {
    return {first.y * second.z - first.z * second.y, first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

[[nodiscard]] auto dot(const Point3& first, const Point3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto property(const compiler::ir::Feature& feature, std::string_view name,
                            double fallback = 0.0) -> double {
    const auto found = std::ranges::find(feature.properties, name, &compiler::ir::Property::name);
    return found == feature.properties.end() ? fallback : found->value.value;
}

struct ExactMetrics {
    double surface_area_mm2{};
    double volume_mm3{};
};

[[nodiscard]] auto profile_area(const compiler::ir::Profile& profile) -> double {
    double twice_area = 0.0;
    for (const auto& segment : profile.segments) {
        if (segment.kind == compiler::ir::ProfileSegmentKind::line) {
            twice_area +=
                segment.start.x_mm * segment.end.y_mm - segment.end.x_mm * segment.start.y_mm;
            continue;
        }
        const double start_angle = std::atan2(segment.start.y_mm - segment.center.y_mm,
                                              segment.start.x_mm - segment.center.x_mm);
        const double end_angle = start_angle + segment.sweep_radians;
        twice_area += segment.radius_mm *
                          (segment.center.x_mm * (std::sin(end_angle) - std::sin(start_angle)) -
                           segment.center.y_mm * (std::cos(end_angle) - std::cos(start_angle))) +
                      segment.radius_mm * segment.radius_mm * segment.sweep_radians;
    }
    return std::abs(twice_area) * 0.5;
}

[[nodiscard]] auto profile_perimeter(const compiler::ir::Profile& profile) -> double {
    double perimeter = 0.0;
    for (const auto& segment : profile.segments) {
        if (segment.kind == compiler::ir::ProfileSegmentKind::line) {
            perimeter += std::hypot(segment.end.x_mm - segment.start.x_mm,
                                    segment.end.y_mm - segment.start.y_mm);
        } else {
            perimeter += segment.radius_mm * std::abs(segment.sweep_radians);
        }
    }
    return perimeter;
}

[[nodiscard]] auto line_profile_centroid_x(const compiler::ir::Profile& profile) -> double {
    double twice_area = 0.0;
    double numerator = 0.0;
    for (const auto& segment : profile.segments) {
        const double cross_value =
            segment.start.x_mm * segment.end.y_mm - segment.end.x_mm * segment.start.y_mm;
        twice_area += cross_value;
        numerator += (segment.start.x_mm + segment.end.x_mm) * cross_value;
    }
    return numerator / (3.0 * twice_area);
}

[[nodiscard]] auto exact_metrics(const compiler::ir::Project& project,
                                 const compiler::ir::Feature& feature) -> ExactMetrics {
    if (feature.type == "BOX") {
        const double width = property(feature, "WIDTH");
        const double depth = property(feature, "DEPTH");
        const double height = property(feature, "HEIGHT");
        return {2.0 * (width * depth + width * height + depth * height), width * depth * height};
    }
    if (feature.type == "CYLINDER") {
        const double radius = property(feature, "RADIUS");
        const double height = property(feature, "HEIGHT");
        return {2.0 * std::numbers::pi * radius * (radius + height),
                std::numbers::pi * radius * radius * height};
    }
    if (feature.type == "CONE") {
        const double first_radius = property(feature, "RADIUS1");
        const double second_radius = property(feature, "RADIUS2");
        const double height = property(feature, "HEIGHT");
        const double slant = std::hypot(height, second_radius - first_radius);
        return {std::numbers::pi * (first_radius * first_radius + second_radius * second_radius +
                                    (first_radius + second_radius) * slant),
                std::numbers::pi * height / 3.0 *
                    (first_radius * first_radius + first_radius * second_radius +
                     second_radius * second_radius)};
    }
    if (feature.type == "SPHERE") {
        const double radius = property(feature, "RADIUS");
        return {4.0 * std::numbers::pi * radius * radius,
                4.0 / 3.0 * std::numbers::pi * radius * radius * radius};
    }
    const auto profile =
        std::ranges::find(project.profiles, feature.profile, &compiler::ir::Profile::name);
    if (profile == project.profiles.end())
        return {};
    double area = profile_area(*profile);
    double perimeter = profile_perimeter(*profile);
    for (const auto& hole_name : feature.region_hole_profiles) {
        const auto hole =
            std::ranges::find(project.profiles, hole_name, &compiler::ir::Profile::name);
        if (hole == project.profiles.end())
            continue;
        area -= profile_area(*hole);
        perimeter += profile_perimeter(*hole);
    }
    if (feature.type == "EXTRUDE") {
        const double height = property(feature, "HEIGHT");
        return {2.0 * area + perimeter * height, area * height};
    }
    if (feature.type == "REVOLVE") {
        const double centroid_radius = line_profile_centroid_x(*profile);
        double surface_area = 0.0;
        for (const auto& segment : profile->segments) {
            const double length = std::hypot(segment.end.x_mm - segment.start.x_mm,
                                             segment.end.y_mm - segment.start.y_mm);
            surface_area += std::numbers::pi * (segment.start.x_mm + segment.end.x_mm) * length;
        }
        return {surface_area, 2.0 * std::numbers::pi * centroid_radius * area};
    }
    return {};
}

[[nodiscard]] auto analyze_part(const Part& part) -> PartAnalysis {
    PartAnalysis analysis;
    analysis.name = part.name;
    analysis.body = part.body;
    analysis.bounds = empty_bounds();
    for (const auto& point : part.vertices) {
        include(analysis.bounds, point);
    }
    double signed_volume = 0.0;
    for (const auto& triangle : part.triangles) {
        const auto& a = part.vertices[triangle[0]];
        const auto& b = part.vertices[triangle[1]];
        const auto& c = part.vertices[triangle[2]];
        const Point3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
        const Point3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
        const auto normal = cross(ab, ac);
        analysis.surface_area_mm2 += 0.5 * std::sqrt(dot(normal, normal));
        signed_volume += dot(a, cross(b, c)) / 6.0;
    }
    analysis.volume_mm3 = std::abs(signed_volume);
    return analysis;
}

} // namespace

auto analyze(const compiler::ir::Project& project) -> ProjectAnalysis {
    return analyze(project, build_model(project));
}

auto analyze(const compiler::ir::Project& project, const Model& model) -> ProjectAnalysis {
    ProjectAnalysis analysis;
    analysis.bounds = empty_bounds();
    std::size_t part_index = 0;
    for (const auto& body : project.bodies) {
        const bool curved_revolve = std::ranges::any_of(body.features, [&](const auto& feature) {
            if (feature.type != "REVOLVE")
                return false;
            const auto profile =
                std::ranges::find(project.profiles, feature.profile, &compiler::ir::Profile::name);
            return profile != project.profiles.end() &&
                   std::ranges::any_of(profile->segments, [](const auto& segment) {
                       return segment.kind == compiler::ir::ProfileSegmentKind::circular_arc;
                   });
        });
        const bool delivery_body = curved_revolve ||
            std::ranges::any_of(body.features, [](const auto& feature) {
            return feature.operation != compiler::ir::FeatureOperation::create ||
                   feature.type == "CHAMFER" || feature.type == "FILLET" ||
                   feature.type == "LINEAR_PATTERN" || feature.type == "MIRROR" ||
                   feature.type == "SWEEP" || feature.type == "LOFT" ||
                   feature.type == "FREEFORM";
        });
        if (delivery_body) {
            while (part_index < model.parts.size() && model.parts[part_index].body == body.name) {
                auto part_analysis = analyze_part(model.parts[part_index]);
                include(analysis.bounds, part_analysis.bounds);
                analysis.surface_area_mm2 += part_analysis.surface_area_mm2;
                analysis.volume_mm3 += part_analysis.volume_mm3;
                analysis.parts.push_back(std::move(part_analysis));
                ++part_index;
            }
            continue;
        }
        for (const auto& feature : body.features) {
            auto part_analysis = analyze_part(model.parts[part_index]);
            const auto metrics = exact_metrics(project, feature);
            part_analysis.surface_area_mm2 = metrics.surface_area_mm2;
            part_analysis.volume_mm3 = metrics.volume_mm3;
            include(analysis.bounds, part_analysis.bounds);
            analysis.surface_area_mm2 += part_analysis.surface_area_mm2;
            analysis.volume_mm3 += part_analysis.volume_mm3;
            analysis.parts.push_back(std::move(part_analysis));
            ++part_index;
        }
    }
    while (part_index < model.parts.size()) {
        auto part_analysis = analyze_part(model.parts[part_index++]);
        include(analysis.bounds, part_analysis.bounds);
        analysis.surface_area_mm2 += part_analysis.surface_area_mm2;
        analysis.volume_mm3 += part_analysis.volume_mm3;
        analysis.parts.push_back(std::move(part_analysis));
    }
    return analysis;
}

auto distance(const Bounds& first, const Bounds& second) -> double {
    double squared = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double gap = std::max({0.0, first.minimum[axis] - second.maximum[axis],
                                     second.minimum[axis] - first.maximum[axis]});
        squared += gap * gap;
    }
    return std::sqrt(squared);
}

} // namespace icad::cad
