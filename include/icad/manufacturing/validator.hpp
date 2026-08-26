#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <filesystem>
#include <cstddef>
#include <string>
#include <vector>

namespace icad::cad {
struct IntersectionAnalysis;
struct ProjectAnalysis;
}

namespace icad::manufacturing {

enum class Severity { information, warning, error };

struct Rules {
    double minimum_extent_mm{0.1};
    double maximum_extent_mm{1'000'000.0};
    double minimum_wall_thickness_mm{0.8};
    double minimum_hole_diameter_mm{1.0};
    double minimum_tool_radius_mm{0.5};
    double minimum_bend_radius_mm{1.0};
    double maximum_overhang_degrees{45.0};
    std::string process{"GENERAL"};
    bool require_material{true};
    bool enforce_process_material_compatibility{true};
};

struct Issue {
    std::string code;
    Severity severity{Severity::information};
    std::string subject;
    std::string message;
};

struct Report {
    bool passed{true};
    std::string process;
    std::size_t checked_rules{};
    std::vector<Issue> issues;
};

[[nodiscard]] auto validate(const compiler::ir::Project& project, const Rules& rules = {})
    -> Report;
[[nodiscard]] auto validate(const compiler::ir::Project& project,
                            const cad::ProjectAnalysis& analysis,
                            const cad::IntersectionAnalysis& intersections,
                            const Rules& rules = {}) -> Report;
[[nodiscard]] auto write_report(const compiler::ir::Project& project,
                                const std::filesystem::path& output,
                                const Rules& rules = {}) -> bool;

} // namespace icad::manufacturing
