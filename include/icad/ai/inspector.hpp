#pragma once

#include "icad/compiler/compiler.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <string_view>

namespace icad::cad {
struct TopologyModel;
}

namespace icad::ai {

[[nodiscard]] auto project_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto topology_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto topology_json(const compiler::ir::Project& project,
                                 const cad::TopologyModel& topology) -> std::string;
[[nodiscard]] auto visual_snapshot_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto comparison_json(const compiler::ir::Project& first,
                                   const compiler::ir::Project& second) -> std::string;
[[nodiscard]] auto diagnostics_json(std::string_view source) -> std::string;

} // namespace icad::ai
