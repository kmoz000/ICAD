#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <array>
#include <string>
#include <vector>

namespace icad::cad {

struct Bounds {
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
};

struct PartAnalysis {
    std::string name;
    std::string body;
    Bounds bounds;
    double surface_area_mm2{};
    double volume_mm3{};
};

struct ProjectAnalysis {
    Bounds bounds;
    std::vector<PartAnalysis> parts;
    double surface_area_mm2{};
    double volume_mm3{};
};

[[nodiscard]] auto analyze(const compiler::ir::Project& project) -> ProjectAnalysis;
[[nodiscard]] auto distance(const Bounds& first, const Bounds& second) -> double;

} // namespace icad::cad
