#pragma once

#include "icad/compiler/compiler.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <string_view>

namespace icad::ai {

[[nodiscard]] auto project_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto topology_json(const compiler::ir::Project& project) -> std::string;
[[nodiscard]] auto diagnostics_json(std::string_view source) -> std::string;

} // namespace icad::ai
