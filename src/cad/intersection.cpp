#include "icad/cad/intersection.hpp"

#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <tuple>
#include <utility>

namespace icad::cad {
namespace {

[[nodiscard]] auto subtract(const Point3& first, const Point3& second) -> Vector3 {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

[[nodiscard]] auto add_scaled(const Point3& point, const Vector3& vector, double scale) -> Point3 {
    return {point.x + vector.x * scale, point.y + vector.y * scale,
            point.z + vector.z * scale};
}

[[nodiscard]] auto dot(const Vector3& first, const Vector3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto cross(const Vector3& first, const Vector3& second) -> Vector3 {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

[[nodiscard]] auto length_squared(const Vector3& vector) -> double { return dot(vector, vector); }

[[nodiscard]] auto distance_squared(const Point3& first, const Point3& second) -> double {
    return length_squared(subtract(first, second));
}

[[nodiscard]] auto triangle_normal(const Triangle3& triangle) -> Vector3 {
    return cross(subtract(triangle.points[1], triangle.points[0]),
                 subtract(triangle.points[2], triangle.points[0]));
}

[[nodiscard]] auto point_in_triangle(const Point3& point, const Triangle3& triangle,
                                     double tolerance_mm) -> bool {
    const auto first = subtract(triangle.points[1], triangle.points[0]);
    const auto second = subtract(triangle.points[2], triangle.points[0]);
    const auto relative = subtract(point, triangle.points[0]);
    const double first_first = dot(first, first);
    const double first_second = dot(first, second);
    const double second_second = dot(second, second);
    const double relative_first = dot(relative, first);
    const double relative_second = dot(relative, second);
    const double denominator = first_first * second_second - first_second * first_second;
    if (std::abs(denominator) <= tolerance_mm * tolerance_mm)
        return false;
    const double first_coordinate =
        (second_second * relative_first - first_second * relative_second) / denominator;
    const double second_coordinate =
        (first_first * relative_second - first_second * relative_first) / denominator;
    const double coordinate_tolerance =
        tolerance_mm / std::max({std::sqrt(first_first), std::sqrt(second_second), 1.0});
    return first_coordinate >= -coordinate_tolerance &&
           second_coordinate >= -coordinate_tolerance &&
           first_coordinate + second_coordinate <= 1.0 + coordinate_tolerance;
}

struct Point2 {
    double x{};
    double y{};
};

[[nodiscard]] auto projected(const Point3& point, std::size_t dropped_axis) -> Point2 {
    if (dropped_axis == 0)
        return {point.y, point.z};
    if (dropped_axis == 1)
        return {point.x, point.z};
    return {point.x, point.y};
}

[[nodiscard]] auto orientation(const Point2& first, const Point2& second,
                               const Point2& third) -> double {
    return (second.x - first.x) * (third.y - first.y) -
           (second.y - first.y) * (third.x - first.x);
}

[[nodiscard]] auto on_segment(const Point2& point, const Point2& start, const Point2& end,
                              double tolerance_mm) -> bool {
    return std::abs(orientation(start, end, point)) <= tolerance_mm &&
           point.x >= std::min(start.x, end.x) - tolerance_mm &&
           point.x <= std::max(start.x, end.x) + tolerance_mm &&
           point.y >= std::min(start.y, end.y) - tolerance_mm &&
           point.y <= std::max(start.y, end.y) + tolerance_mm;
}

[[nodiscard]] auto segments_intersect(const Point2& first_start, const Point2& first_end,
                                      const Point2& second_start, const Point2& second_end,
                                      double tolerance_mm) -> bool {
    const double first_orientation = orientation(first_start, first_end, second_start);
    const double second_orientation = orientation(first_start, first_end, second_end);
    const double third_orientation = orientation(second_start, second_end, first_start);
    const double fourth_orientation = orientation(second_start, second_end, first_end);
    const bool crosses = ((first_orientation > tolerance_mm && second_orientation < -tolerance_mm) ||
                          (first_orientation < -tolerance_mm && second_orientation > tolerance_mm)) &&
                         ((third_orientation > tolerance_mm && fourth_orientation < -tolerance_mm) ||
                          (third_orientation < -tolerance_mm && fourth_orientation > tolerance_mm));
    if (crosses)
        return true;
    return (std::abs(first_orientation) <= tolerance_mm &&
            on_segment(second_start, first_start, first_end, tolerance_mm)) ||
           (std::abs(second_orientation) <= tolerance_mm &&
            on_segment(second_end, first_start, first_end, tolerance_mm)) ||
           (std::abs(third_orientation) <= tolerance_mm &&
            on_segment(first_start, second_start, second_end, tolerance_mm)) ||
           (std::abs(fourth_orientation) <= tolerance_mm &&
            on_segment(first_end, second_start, second_end, tolerance_mm));
}

[[nodiscard]] auto point_in_triangle_2d(const Point2& point, const std::array<Point2, 3>& triangle,
                                        double tolerance_mm) -> bool {
    const double first = orientation(triangle[0], triangle[1], point);
    const double second = orientation(triangle[1], triangle[2], point);
    const double third = orientation(triangle[2], triangle[0], point);
    const bool negative = first < -tolerance_mm || second < -tolerance_mm ||
                          third < -tolerance_mm;
    const bool positive = first > tolerance_mm || second > tolerance_mm || third > tolerance_mm;
    return !(negative && positive);
}

[[nodiscard]] auto coplanar_overlap(const Triangle3& first, const Triangle3& second,
                                    const Vector3& normal, double tolerance_mm) -> bool {
    const double components[]{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
    const auto dropped_axis = static_cast<std::size_t>(
        std::max_element(std::begin(components), std::end(components)) - std::begin(components));
    std::array<Point2, 3> first_projected{};
    std::array<Point2, 3> second_projected{};
    for (std::size_t index = 0; index < 3; ++index) {
        first_projected[index] = projected(first.points[index], dropped_axis);
        second_projected[index] = projected(second.points[index], dropped_axis);
    }
    for (std::size_t first_edge = 0; first_edge < 3; ++first_edge) {
        for (std::size_t second_edge = 0; second_edge < 3; ++second_edge) {
            if (segments_intersect(first_projected[first_edge],
                                   first_projected[(first_edge + 1) % 3],
                                   second_projected[second_edge],
                                   second_projected[(second_edge + 1) % 3], tolerance_mm))
                return true;
        }
    }
    return point_in_triangle_2d(first_projected[0], second_projected, tolerance_mm) ||
           point_in_triangle_2d(second_projected[0], first_projected, tolerance_mm);
}

auto add_unique(std::vector<Point3>& points, const Point3& point, double tolerance_mm) -> void {
    const double tolerance_squared = tolerance_mm * tolerance_mm;
    if (std::ranges::none_of(points, [&](const Point3& existing) {
            return distance_squared(existing, point) <= tolerance_squared;
        })) {
        points.push_back(point);
    }
}

auto collect_edge_intersections(const Triangle3& source, const Triangle3& target,
                                const Plane3& target_plane, double tolerance_mm,
                                std::vector<Point3>& points) -> void {
    for (std::size_t edge = 0; edge < 3; ++edge) {
        const Segment3 segment{source.points[edge], source.points[(edge + 1) % 3]};
        const auto result = intersect(segment, target_plane, tolerance_mm);
        if (result.kind == IntersectionKind::point &&
            point_in_triangle(result.first, target, tolerance_mm)) {
            add_unique(points, result.first, tolerance_mm);
        } else if (result.kind == IntersectionKind::coplanar_overlap) {
            if (point_in_triangle(segment.start, target, tolerance_mm))
                add_unique(points, segment.start, tolerance_mm);
            if (point_in_triangle(segment.end, target, tolerance_mm))
                add_unique(points, segment.end, tolerance_mm);
        }
    }
}

[[nodiscard]] auto triangle_from(const Part& part, const Triangle& indices) -> Triangle3 {
    return {{{part.vertices[indices[0]], part.vertices[indices[1]],
              part.vertices[indices[2]]}}};
}

[[nodiscard]] auto part_bounds(const Part& part) -> Bounds {
    const double high = std::numeric_limits<double>::max();
    const double low = std::numeric_limits<double>::lowest();
    Bounds result{{high, high, high}, {low, low, low}};
    for (const auto& point : part.vertices) {
        const double values[]{point.x, point.y, point.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.minimum[axis] = std::min(result.minimum[axis], values[axis]);
            result.maximum[axis] = std::max(result.maximum[axis], values[axis]);
        }
    }
    return result;
}

[[nodiscard]] auto strictly_inside(const Point3& point, const Part& solid,
                                   double tolerance_mm) -> bool {
    constexpr std::array<Vector3, 3> directions{{{1.0, 0.371, 0.529},
                                                 {0.217, 1.0, 0.413},
                                                 {0.337, 0.193, 1.0}}};
    std::size_t inside_votes = 0;
    for (const auto& direction : directions) {
        std::vector<double> parameters;
        bool boundary = false;
        for (const auto& indices : solid.triangles) {
            const auto result = intersect(Ray3{point, direction}, triangle_from(solid, indices),
                                          tolerance_mm);
            if (!result.intersects())
                continue;
            if (std::abs(result.parameter) <= tolerance_mm) {
                boundary = true;
                break;
            }
            if (result.parameter < tolerance_mm)
                continue;
            if (std::ranges::none_of(parameters, [&](double existing) {
                    return std::abs(existing - result.parameter) <= tolerance_mm;
                })) {
                parameters.push_back(result.parameter);
            }
        }
        if (!boundary && parameters.size() % 2 == 1)
            ++inside_votes;
    }
    return inside_votes >= 2;
}

[[nodiscard]] auto part_has_point_inside(const Part& candidate, const Part& solid,
                                         double tolerance_mm) -> bool {
    if (std::ranges::any_of(candidate.vertices, [&](const Point3& vertex) {
            return strictly_inside(vertex, solid, tolerance_mm);
        })) {
        return true;
    }
    if (candidate.vertices.empty())
        return false;
    Point3 centroid{};
    for (const auto& vertex : candidate.vertices) {
        centroid.x += vertex.x;
        centroid.y += vertex.y;
        centroid.z += vertex.z;
    }
    const double inverse = 1.0 / static_cast<double>(candidate.vertices.size());
    centroid.x *= inverse;
    centroid.y *= inverse;
    centroid.z *= inverse;
    return strictly_inside(centroid, solid, tolerance_mm);
}

} // namespace

SpatialIndex::SpatialIndex(std::span<const Bounds> bounds, double tolerance_mm)
    : tolerance_mm_(std::max(0.0, tolerance_mm)) {
    entries_.reserve(bounds.size());
    for (std::size_t index = 0; index < bounds.size(); ++index) {
        if (valid(bounds[index]))
            entries_.push_back({bounds[index], index});
    }
    std::ranges::sort(entries_, [](const Entry& first, const Entry& second) {
        if (first.bounds.minimum[0] != second.bounds.minimum[0])
            return first.bounds.minimum[0] < second.bounds.minimum[0];
        if (first.bounds.maximum[0] != second.bounds.maximum[0])
            return first.bounds.maximum[0] < second.bounds.maximum[0];
        return first.index < second.index;
    });
}

auto SpatialIndex::query(const Bounds& bounds) const -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    if (!valid(bounds))
        return result;
    for (const auto& entry : entries_) {
        if (entry.bounds.minimum[0] > bounds.maximum[0] + tolerance_mm_)
            break;
        if (overlaps(entry.bounds, bounds, tolerance_mm_))
            result.push_back(entry.index);
    }
    std::ranges::sort(result);
    return result;
}

auto SpatialIndex::overlap_pairs() const -> std::vector<OverlapPair> {
    std::vector<OverlapPair> result;
    for (std::size_t first = 0; first < entries_.size(); ++first) {
        for (std::size_t second = first + 1; second < entries_.size(); ++second) {
            if (entries_[second].bounds.minimum[0] >
                entries_[first].bounds.maximum[0] + tolerance_mm_)
                break;
            if (!overlaps(entries_[first].bounds, entries_[second].bounds, tolerance_mm_))
                continue;
            result.push_back({std::min(entries_[first].index, entries_[second].index),
                              std::max(entries_[first].index, entries_[second].index)});
        }
    }
    std::ranges::sort(result, [](const OverlapPair& first, const OverlapPair& second) {
        return std::tie(first.first, first.second) < std::tie(second.first, second.second);
    });
    return result;
}

auto valid(const Bounds& bounds) -> bool {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis])
            return false;
    }
    return true;
}

auto overlaps(const Bounds& first, const Bounds& second, double tolerance_mm) -> bool {
    if (!valid(first) || !valid(second))
        return false;
    const double tolerance = std::max(0.0, tolerance_mm);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (first.maximum[axis] + tolerance < second.minimum[axis] ||
            second.maximum[axis] + tolerance < first.minimum[axis])
            return false;
    }
    return true;
}

auto bounds_of(const Triangle3& triangle) -> Bounds {
    Bounds result{{triangle.points[0].x, triangle.points[0].y, triangle.points[0].z},
                  {triangle.points[0].x, triangle.points[0].y, triangle.points[0].z}};
    for (std::size_t point = 1; point < 3; ++point) {
        const double values[]{triangle.points[point].x, triangle.points[point].y,
                              triangle.points[point].z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.minimum[axis] = std::min(result.minimum[axis], values[axis]);
            result.maximum[axis] = std::max(result.maximum[axis], values[axis]);
        }
    }
    return result;
}

auto intersect(const Segment3& segment, const Plane3& plane, double tolerance_mm)
    -> IntersectionResult {
    const double tolerance = std::max(0.0, tolerance_mm);
    const double normal_length = std::sqrt(length_squared(plane.normal));
    if (normal_length <= tolerance)
        return {};
    const auto direction = subtract(segment.end, segment.start);
    const double denominator = dot(plane.normal, direction);
    const double start_distance = dot(plane.normal, subtract(segment.start, plane.point));
    const double scaled_tolerance = tolerance * normal_length;
    if (std::abs(denominator) <= scaled_tolerance) {
        if (std::abs(start_distance) <= scaled_tolerance)
            return {IntersectionKind::coplanar_overlap, segment.start, segment.end, 0.0};
        return {};
    }
    const double parameter = -start_distance / denominator;
    if (parameter < -tolerance || parameter > 1.0 + tolerance)
        return {};
    const Point3 point = add_scaled(segment.start, direction, std::clamp(parameter, 0.0, 1.0));
    return {IntersectionKind::point, point, point, parameter};
}

auto intersect(const Ray3& ray, const Triangle3& triangle, double tolerance_mm)
    -> IntersectionResult {
    const double tolerance = std::max(0.0, tolerance_mm);
    if (length_squared(ray.direction) <= tolerance * tolerance)
        return {};
    const auto first_edge = subtract(triangle.points[1], triangle.points[0]);
    const auto second_edge = subtract(triangle.points[2], triangle.points[0]);
    const auto perpendicular = cross(ray.direction, second_edge);
    const double determinant = dot(first_edge, perpendicular);
    if (std::abs(determinant) <= tolerance)
        return {};
    const double inverse = 1.0 / determinant;
    const auto origin_offset = subtract(ray.origin, triangle.points[0]);
    const double first_coordinate = dot(origin_offset, perpendicular) * inverse;
    if (first_coordinate < -tolerance || first_coordinate > 1.0 + tolerance)
        return {};
    const auto second_perpendicular = cross(origin_offset, first_edge);
    const double second_coordinate = dot(ray.direction, second_perpendicular) * inverse;
    if (second_coordinate < -tolerance ||
        first_coordinate + second_coordinate > 1.0 + tolerance)
        return {};
    const double parameter = dot(second_edge, second_perpendicular) * inverse;
    if (parameter < -tolerance)
        return {};
    const Point3 point = add_scaled(ray.origin, ray.direction, std::max(0.0, parameter));
    return {IntersectionKind::point, point, point, parameter};
}

auto intersect(const Triangle3& first, const Triangle3& second, double tolerance_mm)
    -> IntersectionResult {
    const double tolerance = std::max(0.0, tolerance_mm);
    const auto first_normal = triangle_normal(first);
    const auto second_normal = triangle_normal(second);
    const double first_normal_length = std::sqrt(length_squared(first_normal));
    const double second_normal_length = std::sqrt(length_squared(second_normal));
    if (first_normal_length <= tolerance || second_normal_length <= tolerance)
        return {};

    std::array<double, 3> second_distances{};
    std::array<double, 3> first_distances{};
    for (std::size_t index = 0; index < 3; ++index) {
        second_distances[index] =
            dot(first_normal, subtract(second.points[index], first.points[0])) /
            first_normal_length;
        first_distances[index] =
            dot(second_normal, subtract(first.points[index], second.points[0])) /
            second_normal_length;
    }
    const auto separated = [&](const std::array<double, 3>& distances) {
        return std::ranges::all_of(distances,
                                   [&](double value) { return value > tolerance; }) ||
               std::ranges::all_of(distances,
                                   [&](double value) { return value < -tolerance; });
    };
    if (separated(second_distances) || separated(first_distances))
        return {};

    const bool coplanar =
        std::ranges::all_of(second_distances,
                            [&](double value) { return std::abs(value) <= tolerance; }) &&
        std::ranges::all_of(first_distances,
                            [&](double value) { return std::abs(value) <= tolerance; });
    if (coplanar) {
        if (coplanar_overlap(first, second, first_normal, tolerance))
            return {IntersectionKind::coplanar_overlap, first.points[0], first.points[0], 0.0};
        return {};
    }

    std::vector<Point3> points;
    collect_edge_intersections(first, second, {second.points[0], second_normal}, tolerance, points);
    collect_edge_intersections(second, first, {first.points[0], first_normal}, tolerance, points);
    if (points.empty())
        return {};
    if (points.size() == 1)
        return {IntersectionKind::point, points[0], points[0], 0.0};

    std::pair<std::size_t, std::size_t> farthest{0, 1};
    double farthest_distance = distance_squared(points[0], points[1]);
    for (std::size_t first_index = 0; first_index < points.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1; second_index < points.size();
             ++second_index) {
            const double candidate = distance_squared(points[first_index], points[second_index]);
            if (candidate > farthest_distance) {
                farthest = {first_index, second_index};
                farthest_distance = candidate;
            }
        }
    }
    if (farthest_distance <= tolerance * tolerance)
        return {IntersectionKind::point, points[farthest.first], points[farthest.first], 0.0};
    return {IntersectionKind::segment, points[farthest.first], points[farthest.second], 0.0};
}

auto analyze_intersections(const compiler::ir::Project& project, double tolerance_mm)
    -> IntersectionAnalysis {
    const auto model = build_model(project);
    IntersectionAnalysis analysis;
    std::vector<Bounds> model_part_bounds;
    model_part_bounds.reserve(model.parts.size());
    for (const auto& part : model.parts)
        model_part_bounds.push_back(part_bounds(part));
    const SpatialIndex part_index{model_part_bounds, tolerance_mm};
    const auto part_pairs = part_index.overlap_pairs();
    analysis.part_pair_candidates = part_pairs.size();
    using BodyPair = std::pair<std::string, std::string>;
    std::map<BodyPair, IntersectionAnalysis::BodyContact> body_contacts;

    for (const auto& pair : part_pairs) {
        const auto& first_part = model.parts[pair.first];
        const auto& second_part = model.parts[pair.second];
        const bool strict_bounds_overlap = [&] {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double depth = std::min(model_part_bounds[pair.first].maximum[axis],
                                              model_part_bounds[pair.second].maximum[axis]) -
                                     std::max(model_part_bounds[pair.first].minimum[axis],
                                              model_part_bounds[pair.second].minimum[axis]);
                if (depth <= tolerance_mm)
                    return false;
            }
            return true;
        }();
        IntersectionAnalysis::BodyContact* body_contact = nullptr;
        if (first_part.body != second_part.body) {
            const BodyPair body_pair{std::min(first_part.body, second_part.body),
                                     std::max(first_part.body, second_part.body)};
            auto& contact = body_contacts[body_pair];
            contact.first_body = body_pair.first;
            contact.second_body = body_pair.second;
            ++contact.part_pair_candidates;
            body_contact = &contact;
        }
        std::vector<Triangle3> second_triangles;
        std::vector<Bounds> second_bounds;
        second_triangles.reserve(second_part.triangles.size());
        second_bounds.reserve(second_part.triangles.size());
        for (const auto& triangle : second_part.triangles) {
            second_triangles.push_back(triangle_from(second_part, triangle));
            second_bounds.push_back(bounds_of(second_triangles.back()));
        }
        const SpatialIndex triangle_index{second_bounds, tolerance_mm};
        bool part_intersects = false;
        bool crossing_surfaces = false;
        for (const auto& indices : first_part.triangles) {
            const auto first_triangle = triangle_from(first_part, indices);
            for (const std::size_t second_index : triangle_index.query(bounds_of(first_triangle))) {
                ++analysis.triangle_pair_candidates;
                if (body_contact != nullptr)
                    ++body_contact->triangle_pair_candidates;
                const auto result =
                    intersect(first_triangle, second_triangles[second_index], tolerance_mm);
                if (!result.intersects())
                    continue;
                ++analysis.intersecting_triangle_pairs;
                if (body_contact != nullptr)
                    ++body_contact->intersecting_triangle_pairs;
                part_intersects = true;
                if (result.kind == IntersectionKind::segment &&
                    strict_bounds_overlap &&
                    distance_squared(result.first, result.second) > tolerance_mm * tolerance_mm) {
                    crossing_surfaces = true;
                }
            }
        }
        if (part_intersects) {
            ++analysis.intersecting_part_pairs;
            if (body_contact != nullptr)
                ++body_contact->intersecting_part_pairs;
        }
        const bool contained = body_contact != nullptr &&
                               (part_has_point_inside(first_part, second_part, tolerance_mm) ||
                                part_has_point_inside(second_part, first_part, tolerance_mm));
        const bool penetrating = body_contact != nullptr && (crossing_surfaces || contained);
        if (penetrating) {
            ++analysis.penetrating_part_pairs;
            if (body_contact != nullptr)
                ++body_contact->penetrating_part_pairs;
        }
        if (contained) {
            ++analysis.contained_part_pairs;
            if (body_contact != nullptr)
                ++body_contact->contained_part_pairs;
        }
        if (body_contact != nullptr && part_intersects && !penetrating) {
            ++analysis.surface_contact_only_part_pairs;
            if (body_contact != nullptr)
                ++body_contact->surface_contact_only_part_pairs;
        }
    }
    for (auto& [key, contact] : body_contacts) {
        static_cast<void>(key);
        analysis.body_contacts.push_back(std::move(contact));
    }
    return analysis;
}

} // namespace icad::cad
