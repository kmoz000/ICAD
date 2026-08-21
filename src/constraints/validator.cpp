#include "icad/constraints/validator.hpp"

#include "icad/cad/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <string_view>

namespace icad::constraints {
namespace {

[[nodiscard]] auto selector_axis(std::string_view selector) -> std::size_t {
    if (selector.starts_with('Y'))
        return 1;
    if (selector.starts_with('Z'))
        return 2;
    return 0;
}

[[nodiscard]] auto face_coordinate(const cad::Bounds& bounds, std::string_view selector)
    -> double {
    const std::size_t axis = selector_axis(selector);
    return selector.ends_with("MAX") ? bounds.maximum[axis] : bounds.minimum[axis];
}

[[nodiscard]] auto edge_coordinate(const cad::Bounds& bounds, std::string_view selector,
                                   std::size_t axis) -> std::optional<double> {
    if (axis == selector_axis(selector))
        return std::nullopt;
    const char name = "XYZ"[axis];
    const std::string prefix{std::string{name} + "_"};
    const auto at = selector.find(prefix);
    if (at == std::string_view::npos)
        return std::nullopt;
    const bool maximum = selector.substr(at + prefix.size()).starts_with("MAX");
    return maximum ? bounds.maximum[axis] : bounds.minimum[axis];
}

} // namespace

auto validate(const compiler::ir::Project& project) -> std::vector<Result> {
    const auto analysis = cad::analyze(project);
    std::map<std::string, cad::Bounds> body_bounds;
    std::map<std::string, bool> initialized;
    for (const auto& part : analysis.parts) {
        if (!initialized[part.body]) {
            body_bounds[part.body] = part.bounds;
            initialized[part.body] = true;
            continue;
        }
        auto& bounds = body_bounds[part.body];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            bounds.minimum[axis] = std::min(bounds.minimum[axis], part.bounds.minimum[axis]);
            bounds.maximum[axis] = std::max(bounds.maximum[axis], part.bounds.maximum[axis]);
        }
    }

    std::vector<Result> results;
    for (const auto& constraint : project.constraints) {
        double actual = 0.0;
        double required = constraint.target_value;
        bool passed = false;
        std::string unit = constraint.target_unit;
        std::string message;
        if (constraint.kind == "MIN_DISTANCE") {
            required = constraint.minimum_mm;
            actual = cad::distance(body_bounds.at(constraint.first_body),
                                   body_bounds.at(constraint.second_body));
            passed = actual + 1e-9 >= required;
            message = passed ? "minimum distance satisfied" : "minimum distance violated";
        } else if (constraint.kind == "COINCIDENT") {
            const auto first = std::ranges::find(project.points, constraint.first_body,
                                                 &compiler::ir::SpatialPoint::name);
            const auto second = std::ranges::find(project.points, constraint.second_body,
                                                  &compiler::ir::SpatialPoint::name);
            actual = std::hypot(first->position_mm[0] - second->position_mm[0],
                                first->position_mm[1] - second->position_mm[1],
                                first->position_mm[2] - second->position_mm[2]);
            passed = actual <= constraint.target_value + 1e-9;
            message = passed ? "points coincide within tolerance"
                             : "point coincidence tolerance violated";
        } else {
            const auto first = std::ranges::find(project.vectors, constraint.first_body,
                                                 &compiler::ir::Direction::name);
            const auto second = std::ranges::find(project.vectors, constraint.second_body,
                                                  &compiler::ir::Direction::name);
            const double dot =
                std::clamp(first->unit[0] * second->unit[0] + first->unit[1] * second->unit[1] +
                               first->unit[2] * second->unit[2],
                           -1.0, 1.0);
            const double angle = std::acos(dot) * 180.0 / std::numbers::pi;
            unit = "deg";
            if (constraint.kind == "PARALLEL") {
                actual = std::min(angle, 180.0 - angle);
                passed = actual <= 1e-9;
                message = passed ? "vectors are parallel" : "vectors are not parallel";
            } else if (constraint.kind == "PERPENDICULAR") {
                actual = std::abs(angle - 90.0);
                passed = actual <= 1e-9;
                message = passed ? "vectors are perpendicular" : "vectors are not perpendicular";
            } else {
                actual = angle;
                passed = std::abs(actual - constraint.target_value) <= 1e-9;
                message = passed ? "vector angle target satisfied" : "vector angle target violated";
            }
        }
        results.push_back(
            Result{constraint.name, passed, required, actual, std::move(unit), std::move(message)});
    }
    for (const auto& mate : project.mates) {
        const auto first = body_bounds.find(mate.first_occurrence);
        const auto second = body_bounds.find(mate.second_occurrence);
        if (first == body_bounds.end() || second == body_bounds.end()) {
            results.push_back(Result{mate.name, false, mate.target_mm, 0.0, "mm",
                                     "mate occurrence has no delivery geometry"});
            continue;
        }
        double actual = 0.0;
        bool passed = false;
        std::string message;
        if (mate.kind == compiler::ir::MateKind::face) {
            actual = std::abs(face_coordinate(first->second, mate.first_selector) -
                              face_coordinate(second->second, mate.second_selector));
            passed = std::abs(actual - mate.target_mm) <= project.tolerance.linear_mm;
            message = passed ? "face offset mate satisfied" : "face offset mate violated";
        } else {
            double squared = 0.0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto first_coordinate =
                    edge_coordinate(first->second, mate.first_selector, axis);
                const auto second_coordinate =
                    edge_coordinate(second->second, mate.second_selector, axis);
                if (!first_coordinate || !second_coordinate)
                    continue;
                const double delta = *first_coordinate - *second_coordinate;
                squared += delta * delta;
            }
            actual = std::sqrt(squared);
            passed = actual <= mate.target_mm + project.tolerance.linear_mm;
            message = passed ? "edge coincidence mate satisfied"
                             : "edge coincidence mate violated";
        }
        results.push_back(Result{mate.name, passed, mate.target_mm, actual, "mm",
                                 std::move(message)});
    }
    return results;
}

auto all_passed(const std::vector<Result>& results) -> bool {
    return std::ranges::all_of(results, &Result::passed);
}

} // namespace icad::constraints
