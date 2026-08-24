#include "icad/constraints/sketch_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace icad::constraints {
namespace {

using Matrix = std::vector<std::vector<double>>;

struct Variables {
    std::vector<std::pair<std::size_t, std::size_t>> coordinate;
    std::unordered_map<std::string, std::size_t> point_index;
};

[[nodiscard]] auto variables_for(const compiler::ir::Sketch& sketch) -> Variables {
    Variables variables;
    for (std::size_t index = 0; index < sketch.points.size(); ++index) {
        variables.point_index.emplace(sketch.points[index].name, index);
        if (!sketch.points[index].fixed) {
            variables.coordinate.emplace_back(index, 0);
            variables.coordinate.emplace_back(index, 1);
        }
    }
    return variables;
}

[[nodiscard]] auto point(const compiler::ir::Sketch& sketch, const Variables& variables,
                         const std::string& name) -> const compiler::ir::Point2& {
    return sketch.points[variables.point_index.at(name)].solved;
}

[[nodiscard]] auto entity(const compiler::ir::Sketch& sketch, const std::string& name)
    -> const compiler::ir::SketchEntity& {
    return *std::ranges::find(sketch.entities, name, &compiler::ir::SketchEntity::name);
}

struct Vector2 {
    double x{};
    double y{};
};

[[nodiscard]] auto line_vector(const compiler::ir::Sketch& sketch, const Variables& variables,
                               const compiler::ir::SketchEntity& line) -> Vector2 {
    const auto& start = point(sketch, variables, line.start);
    const auto& end = point(sketch, variables, line.end);
    return {end.x_mm - start.x_mm, end.y_mm - start.y_mm};
}

[[nodiscard]] auto length(Vector2 value) -> double {
    return std::hypot(value.x, value.y);
}

[[nodiscard]] auto center(const compiler::ir::Sketch& sketch, const Variables& variables,
                          const compiler::ir::SketchEntity& circular)
    -> const compiler::ir::Point2& {
    return point(sketch, variables, circular.center);
}

[[nodiscard]] auto radius(const compiler::ir::Sketch& sketch, const Variables& variables,
                          const compiler::ir::SketchEntity& circular) -> double {
    if (circular.full_circle)
        return circular.radius_mm;
    const auto& arc_center = center(sketch, variables, circular);
    const auto& start = point(sketch, variables, circular.start);
    return std::hypot(start.x_mm - arc_center.x_mm, start.y_mm - arc_center.y_mm);
}

[[nodiscard]] auto residuals(const compiler::ir::Sketch& sketch, const Variables& variables)
    -> std::vector<double> {
    std::vector<double> values;
    for (const auto& constraint : sketch.constraints) {
        if (constraint.kind == "HORIZONTAL") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(second.y_mm - first.y_mm);
        } else if (constraint.kind == "VERTICAL") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(second.x_mm - first.x_mm);
        } else if (constraint.kind == "COINCIDENT") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(second.x_mm - first.x_mm);
            values.push_back(second.y_mm - first.y_mm);
        } else if (constraint.kind == "DISTANCE") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(std::hypot(second.x_mm - first.x_mm,
                                        second.y_mm - first.y_mm) -
                             constraint.target_value);
        } else if (constraint.kind == "H_DISTANCE") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(second.x_mm - first.x_mm - constraint.target_value);
        } else if (constraint.kind == "V_DISTANCE") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            values.push_back(second.y_mm - first.y_mm - constraint.target_value);
        } else if (constraint.kind == "ANGLE") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            const auto& third = point(sketch, variables, constraint.references[2]);
            const double first_x = first.x_mm - second.x_mm;
            const double first_y = first.y_mm - second.y_mm;
            const double second_x = third.x_mm - second.x_mm;
            const double second_y = third.y_mm - second.y_mm;
            const double first_length = std::hypot(first_x, first_y);
            const double second_length = std::hypot(second_x, second_y);
            if (first_length * second_length <= 1e-12) {
                values.push_back(1e6);
                continue;
            }
            const double cosine = std::clamp((first_x * second_x + first_y * second_y) /
                                                 (first_length * second_length),
                                             -1.0, 1.0);
            const double difference = std::acos(cosine) -
                                      constraint.target_value * std::numbers::pi / 180.0;
            // Ten millimetres per radian keeps angular and dimensional
            // equations numerically comparable without changing their roots.
            values.push_back(difference * 10.0);
        } else if (constraint.kind == "PARALLEL" ||
                   constraint.kind == "PERPENDICULAR" ||
                   constraint.kind == "EQUAL_LENGTH") {
            const auto& first_entity = entity(sketch, constraint.references[0]);
            const auto& second_entity = entity(sketch, constraint.references[1]);
            const Vector2 first = line_vector(sketch, variables, first_entity);
            const Vector2 second = line_vector(sketch, variables, second_entity);
            const double first_length = length(first);
            const double second_length = length(second);
            if (first_length * second_length <= 1e-12) {
                values.push_back(1e6);
                continue;
            }
            if (constraint.kind == "PARALLEL") {
                values.push_back((first.x * second.y - first.y * second.x) * 10.0 /
                                 (first_length * second_length));
            } else if (constraint.kind == "PERPENDICULAR") {
                values.push_back((first.x * second.x + first.y * second.y) * 10.0 /
                                 (first_length * second_length));
            } else {
                values.push_back(first_length - second_length);
            }
        } else if (constraint.kind == "CONCENTRIC" ||
                   constraint.kind == "EQUAL_RADIUS") {
            const auto& first_entity = entity(sketch, constraint.references[0]);
            const auto& second_entity = entity(sketch, constraint.references[1]);
            if (constraint.kind == "CONCENTRIC") {
                const auto& first = center(sketch, variables, first_entity);
                const auto& second = center(sketch, variables, second_entity);
                values.push_back(second.x_mm - first.x_mm);
                values.push_back(second.y_mm - first.y_mm);
            } else {
                values.push_back(radius(sketch, variables, first_entity) -
                                 radius(sketch, variables, second_entity));
            }
        } else if (constraint.kind == "MIDPOINT") {
            const auto& candidate = point(sketch, variables, constraint.references[0]);
            const auto& line = entity(sketch, constraint.references[1]);
            const auto& start = point(sketch, variables, line.start);
            const auto& end = point(sketch, variables, line.end);
            values.push_back(candidate.x_mm - (start.x_mm + end.x_mm) * 0.5);
            values.push_back(candidate.y_mm - (start.y_mm + end.y_mm) * 0.5);
        } else if (constraint.kind == "SYMMETRIC") {
            const auto& first = point(sketch, variables, constraint.references[0]);
            const auto& second = point(sketch, variables, constraint.references[1]);
            const auto& axis = entity(sketch, constraint.references[2]);
            const auto& axis_start = point(sketch, variables, axis.start);
            const Vector2 axis_vector = line_vector(sketch, variables, axis);
            const double axis_length = length(axis_vector);
            if (axis_length <= 1e-12) {
                values.push_back(1e6);
                values.push_back(1e6);
                continue;
            }
            const Vector2 midpoint{(first.x_mm + second.x_mm) * 0.5 - axis_start.x_mm,
                                   (first.y_mm + second.y_mm) * 0.5 - axis_start.y_mm};
            const Vector2 pair{second.x_mm - first.x_mm, second.y_mm - first.y_mm};
            values.push_back((axis_vector.x * midpoint.y - axis_vector.y * midpoint.x) /
                             axis_length);
            values.push_back((axis_vector.x * pair.x + axis_vector.y * pair.y) /
                             axis_length);
        } else if (constraint.kind == "TANGENT") {
            const auto& first = entity(sketch, constraint.references[0]);
            const auto& second = entity(sketch, constraint.references[1]);
            const auto& line = first.kind == compiler::ir::ProfileSegmentKind::line
                                   ? first
                                   : second;
            const auto& arc = first.kind == compiler::ir::ProfileSegmentKind::circular_arc
                                  ? first
                                  : second;
            const Vector2 direction = line_vector(sketch, variables, line);
            const auto& contact = point(sketch, variables, constraint.references[2]);
            const auto& arc_center = center(sketch, variables, arc);
            const Vector2 radial{contact.x_mm - arc_center.x_mm,
                                 contact.y_mm - arc_center.y_mm};
            const double denominator = length(direction) * length(radial);
            if (denominator <= 1e-12) {
                values.push_back(1e6);
                continue;
            }
            values.push_back((direction.x * radial.x + direction.y * radial.y) * 10.0 /
                             denominator);
        }
    }
    return values;
}

auto set_coordinate(compiler::ir::Sketch& sketch,
                    const std::pair<std::size_t, std::size_t>& variable, double value) -> void {
    auto& point_value = sketch.points[variable.first].solved;
    if (variable.second == 0)
        point_value.x_mm = value;
    else
        point_value.y_mm = value;
}

[[nodiscard]] auto get_coordinate(const compiler::ir::Sketch& sketch,
                                  const std::pair<std::size_t, std::size_t>& variable) -> double {
    const auto& point_value = sketch.points[variable.first].solved;
    return variable.second == 0 ? point_value.x_mm : point_value.y_mm;
}

[[nodiscard]] auto jacobian(compiler::ir::Sketch& sketch, const Variables& variables,
                            const std::vector<double>& base) -> Matrix {
    Matrix matrix(base.size(), std::vector<double>(variables.coordinate.size(), 0.0));
    constexpr double step = 1e-6;
    for (std::size_t column = 0; column < variables.coordinate.size(); ++column) {
        const double original = get_coordinate(sketch, variables.coordinate[column]);
        set_coordinate(sketch, variables.coordinate[column], original + step);
        const auto shifted = residuals(sketch, variables);
        set_coordinate(sketch, variables.coordinate[column], original);
        for (std::size_t row = 0; row < base.size(); ++row)
            matrix[row][column] = (shifted[row] - base[row]) / step;
    }
    return matrix;
}

[[nodiscard]] auto solve_linear(Matrix matrix, std::vector<double> values)
    -> std::vector<double> {
    const std::size_t size = values.size();
    for (std::size_t pivot = 0; pivot < size; ++pivot) {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1; row < size; ++row) {
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot]))
                best = row;
        }
        if (std::abs(matrix[best][pivot]) < 1e-15)
            continue;
        std::swap(matrix[pivot], matrix[best]);
        std::swap(values[pivot], values[best]);
        const double divisor = matrix[pivot][pivot];
        for (std::size_t column = pivot; column < size; ++column)
            matrix[pivot][column] /= divisor;
        values[pivot] /= divisor;
        for (std::size_t row = 0; row < size; ++row) {
            if (row == pivot)
                continue;
            const double factor = matrix[row][pivot];
            for (std::size_t column = pivot; column < size; ++column)
                matrix[row][column] -= factor * matrix[pivot][column];
            values[row] -= factor * values[pivot];
        }
    }
    return values;
}

[[nodiscard]] auto rank(Matrix matrix) -> std::size_t {
    if (matrix.empty() || matrix.front().empty())
        return 0;
    const std::size_t rows = matrix.size();
    const std::size_t columns = matrix.front().size();
    std::size_t pivot_row = 0;
    for (std::size_t column = 0; column < columns && pivot_row < rows; ++column) {
        std::size_t best = pivot_row;
        for (std::size_t row = pivot_row + 1; row < rows; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[best][column]))
                best = row;
        }
        if (std::abs(matrix[best][column]) < 1e-7)
            continue;
        std::swap(matrix[pivot_row], matrix[best]);
        const double divisor = matrix[pivot_row][column];
        for (std::size_t next = column; next < columns; ++next)
            matrix[pivot_row][next] /= divisor;
        for (std::size_t row = pivot_row + 1; row < rows; ++row) {
            const double factor = matrix[row][column];
            for (std::size_t next = column; next < columns; ++next)
                matrix[row][next] -= factor * matrix[pivot_row][next];
        }
        ++pivot_row;
    }
    return pivot_row;
}

[[nodiscard]] auto maximum_absolute(const std::vector<double>& values) -> double {
    double maximum = 0.0;
    for (const double value : values)
        maximum = std::max(maximum, std::abs(value));
    return maximum;
}

} // namespace

auto solve_sketch(compiler::ir::Sketch& sketch) -> void {
    const Variables variables = variables_for(sketch);
    constexpr std::size_t maximum_iterations = 80;
    constexpr double convergence_tolerance = 1e-8;
    constexpr double damping = 1e-8;

    for (sketch.iterations = 0; sketch.iterations < maximum_iterations; ++sketch.iterations) {
        const auto current = residuals(sketch, variables);
        sketch.maximum_residual = maximum_absolute(current);
        if (sketch.maximum_residual <= convergence_tolerance)
            break;
        const Matrix derivatives = jacobian(sketch, variables, current);
        const std::size_t count = variables.coordinate.size();
        Matrix normal(count, std::vector<double>(count, 0.0));
        std::vector<double> right(count, 0.0);
        for (std::size_t row = 0; row < derivatives.size(); ++row) {
            for (std::size_t first = 0; first < count; ++first) {
                right[first] -= derivatives[row][first] * current[row];
                for (std::size_t second = 0; second < count; ++second)
                    normal[first][second] += derivatives[row][first] * derivatives[row][second];
            }
        }
        for (std::size_t diagonal = 0; diagonal < count; ++diagonal)
            normal[diagonal][diagonal] += damping;
        const auto delta = solve_linear(std::move(normal), std::move(right));
        for (std::size_t index = 0; index < count; ++index) {
            const double updated = get_coordinate(sketch, variables.coordinate[index]) + delta[index];
            set_coordinate(sketch, variables.coordinate[index], updated);
        }
    }

    const auto final_residuals = residuals(sketch, variables);
    sketch.maximum_residual = maximum_absolute(final_residuals);
    const Matrix final_jacobian = jacobian(sketch, variables, final_residuals);
    const std::size_t equation_rank = rank(final_jacobian);
    sketch.degrees_of_freedom = variables.coordinate.size() > equation_rank
                                    ? variables.coordinate.size() - equation_rank
                                    : 0;
    if (!std::isfinite(sketch.maximum_residual) || sketch.maximum_residual > 1e-6) {
        sketch.status = compiler::ir::SketchSolveStatus::inconsistent;
    } else if (sketch.degrees_of_freedom == 0) {
        sketch.status = compiler::ir::SketchSolveStatus::fully_constrained;
    } else {
        sketch.status = compiler::ir::SketchSolveStatus::under_constrained;
    }
}

} // namespace icad::constraints
