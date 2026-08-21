#include "icad/cad/queries.hpp"

#include "model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icad::cad {
namespace {

[[nodiscard]] auto subtract(const Point3& first, const Point3& second) -> Vector3 {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

[[nodiscard]] auto add(const Point3& point, const Vector3& direction, double scale) -> Point3 {
    return {point.x + direction.x * scale, point.y + direction.y * scale,
            point.z + direction.z * scale};
}

[[nodiscard]] auto dot(const Vector3& first, const Vector3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto squared_distance(const Point3& first, const Point3& second) -> double {
    const auto delta = subtract(first, second);
    return dot(delta, delta);
}

[[nodiscard]] auto closest_on_triangle(const Point3& point, const Triangle3& triangle) -> Point3 {
    const auto& a = triangle.points[0];
    const auto& b = triangle.points[1];
    const auto& c = triangle.points[2];
    const auto ab = subtract(b, a);
    const auto ac = subtract(c, a);
    const auto ap = subtract(point, a);
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
        return a;
    const auto bp = subtract(point, b);
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
        return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
        return add(a, ab, d1 / (d1 - d3));
    const auto cp = subtract(point, c);
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
        return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
        return add(a, ac, d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        const auto bc = subtract(c, b);
        return add(b, bc, (d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const double denominator = 1.0 / (va + vb + vc);
    return add(add(a, ab, vb * denominator), ac, vc * denominator);
}

auto closest_segments(const Segment3& first, const Segment3& second, Point3& on_first,
                      Point3& on_second) -> void {
    const auto d1 = subtract(first.end, first.start);
    const auto d2 = subtract(second.end, second.start);
    const auto r = subtract(first.start, second.start);
    const double a = dot(d1, d1);
    const double e = dot(d2, d2);
    const double f = dot(d2, r);
    double first_parameter = 0.0;
    double second_parameter = 0.0;
    constexpr double epsilon = 1e-18;
    if (a <= epsilon && e <= epsilon) {
        on_first = first.start;
        on_second = second.start;
        return;
    }
    if (a <= epsilon) {
        second_parameter = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = dot(d1, r);
        if (e <= epsilon) {
            first_parameter = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = dot(d1, d2);
            const double denominator = a * e - b * b;
            if (std::abs(denominator) > epsilon)
                first_parameter = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);
            second_parameter = (b * first_parameter + f) / e;
            if (second_parameter < 0.0) {
                second_parameter = 0.0;
                first_parameter = std::clamp(-c / a, 0.0, 1.0);
            } else if (second_parameter > 1.0) {
                second_parameter = 1.0;
                first_parameter = std::clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }
    on_first = add(first.start, d1, first_parameter);
    on_second = add(second.start, d2, second_parameter);
}

auto update_closest(const Point3& first, const Point3& second, double& best_squared,
                    Point3& best_first, Point3& best_second) -> void {
    const double candidate = squared_distance(first, second);
    if (candidate < best_squared) {
        best_squared = candidate;
        best_first = first;
        best_second = second;
    }
}

[[nodiscard]] auto triangle_of(const Part& part, const Triangle& indices) -> Triangle3 {
    return {{part.vertices[indices[0]], part.vertices[indices[1]], part.vertices[indices[2]]}};
}

auto triangle_distance(const Triangle3& first, const Triangle3& second, double tolerance,
                       double& best_squared, Point3& best_first, Point3& best_second) -> void {
    const auto crossing = intersect(first, second, tolerance);
    if (crossing.intersects()) {
        best_squared = 0.0;
        best_first = crossing.first;
        best_second = crossing.first;
        return;
    }
    for (const auto& vertex : first.points)
        update_closest(vertex, closest_on_triangle(vertex, second), best_squared, best_first,
                       best_second);
    for (const auto& vertex : second.points)
        update_closest(closest_on_triangle(vertex, first), vertex, best_squared, best_first,
                       best_second);
    constexpr std::array<std::array<std::size_t, 2>, 3> edges{{{0, 1}, {1, 2}, {2, 0}}};
    for (const auto& first_edge : edges) {
        for (const auto& second_edge : edges) {
            Point3 first_point;
            Point3 second_point;
            closest_segments({first.points[first_edge[0]], first.points[first_edge[1]]},
                             {second.points[second_edge[0]], second.points[second_edge[1]]},
                             first_point, second_point);
            update_closest(first_point, second_point, best_squared, best_first, best_second);
        }
    }
}

[[nodiscard]] auto near(const Point3& first, const Point3& second, double tolerance) -> bool {
    return squared_distance(first, second) <= tolerance * tolerance;
}

auto append_unique(std::vector<Point3>& points, const Point3& point, double tolerance) -> void {
    if (!std::ranges::any_of(points,
                             [&](const auto& existing) { return near(existing, point, tolerance); }))
        points.push_back(point);
}

} // namespace

auto exact_polyhedral_distance(const compiler::ir::Project& project, std::string_view first_body,
                               std::string_view second_body) -> DistanceQuery {
    DistanceQuery result;
    result.first_body = first_body;
    result.second_body = second_body;
    if (first_body == second_body)
        return result;
    const auto model = build_model(project);
    double best_squared = std::numeric_limits<double>::infinity();
    for (const auto& first_part : model.parts) {
        if (first_part.body != first_body)
            continue;
        for (const auto& second_part : model.parts) {
            if (second_part.body != second_body)
                continue;
            result.found = true;
            for (const auto& first_indices : first_part.triangles) {
                const auto first_triangle = triangle_of(first_part, first_indices);
                for (const auto& second_indices : second_part.triangles) {
                    triangle_distance(first_triangle, triangle_of(second_part, second_indices),
                                      project.tolerance.linear_mm, best_squared,
                                      result.first_point, result.second_point);
                    if (best_squared == 0.0)
                        break;
                }
                if (best_squared == 0.0)
                    break;
            }
        }
    }
    if (result.found)
        result.distance_mm = std::sqrt(best_squared);
    return result;
}

auto section(const compiler::ir::Project& project, const Plane3& requested,
             std::string_view body) -> SectionQuery {
    SectionQuery result;
    result.tolerance_mm = project.tolerance.linear_mm;
    const double magnitude = std::hypot(requested.normal.x, requested.normal.y,
                                        requested.normal.z);
    if (magnitude <= 1e-15)
        return result;
    result.plane = {requested.point,
                    {requested.normal.x / magnitude, requested.normal.y / magnitude,
                     requested.normal.z / magnitude}};
    const auto model = build_model(project);
    constexpr std::array<std::array<std::size_t, 2>, 3> edges{{{0, 1}, {1, 2}, {2, 0}}};
    for (const auto& part : model.parts) {
        if (!body.empty() && part.body != body)
            continue;
        for (const auto& indices : part.triangles) {
            const auto triangle = triangle_of(part, indices);
            std::array<double, 3> distances{};
            for (std::size_t vertex = 0; vertex < 3; ++vertex)
                distances[vertex] = dot(subtract(triangle.points[vertex], result.plane.point),
                                        result.plane.normal);
            std::vector<Point3> points;
            for (const auto& edge : edges) {
                const std::size_t first = edge[0];
                const std::size_t second = edge[1];
                if (std::abs(distances[first]) <= result.tolerance_mm)
                    append_unique(points, triangle.points[first], result.tolerance_mm);
                if ((distances[first] < -result.tolerance_mm &&
                     distances[second] > result.tolerance_mm) ||
                    (distances[first] > result.tolerance_mm &&
                     distances[second] < -result.tolerance_mm)) {
                    const double parameter =
                        distances[first] / (distances[first] - distances[second]);
                    append_unique(points,
                                  add(triangle.points[first],
                                      subtract(triangle.points[second], triangle.points[first]),
                                      parameter),
                                  result.tolerance_mm);
                }
            }
            if (points.size() < 2)
                continue;
            std::size_t first = 0;
            std::size_t second = 1;
            double longest = squared_distance(points[0], points[1]);
            for (std::size_t a = 0; a < points.size(); ++a) {
                for (std::size_t b = a + 1; b < points.size(); ++b) {
                    const double length = squared_distance(points[a], points[b]);
                    if (length > longest) {
                        longest = length;
                        first = a;
                        second = b;
                    }
                }
            }
            if (longest > result.tolerance_mm * result.tolerance_mm)
                result.segments.push_back({part.body, part.name, {points[first], points[second]}});
        }
    }
    return result;
}

} // namespace icad::cad
