#pragma once

#include "icad/cad/geometry.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace icad::cad {

using Triangle = std::array<std::size_t, 3>;

struct Part {
    std::string name;
    std::string body;
    std::string material;
    std::string feature_type;
    std::vector<Point3> vertices;
    std::vector<Triangle> triangles;
    bool boolean_result{};
    bool faceted_result{};
    std::vector<std::string> repairs;
};

struct Model {
    std::vector<Part> parts;

    [[nodiscard]] auto vertex_count() const -> std::size_t;
    [[nodiscard]] auto triangle_count() const -> std::size_t;
};

[[nodiscard]] auto build_model(const compiler::ir::Project& project) -> Model;
[[nodiscard]] auto is_valid(const Model& model) -> bool;

} // namespace icad::cad
