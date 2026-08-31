#pragma once

#include "icad/exchange/exporter.hpp"

namespace icad::exchange {

[[nodiscard]] auto write_step(const compiler::ir::Project& project,
                              const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto write_step(const compiler::ir::Project& project, const cad::Model& model,
                              const cad::TopologyModel& topology,
                              const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto write_assembly_step(const compiler::ir::Project& project,
                                       const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto write_assembly_step(const compiler::ir::Project& project,
                                       const cad::Model& model,
                                       const cad::TopologyModel& topology,
                                       const std::filesystem::path& output) -> ExportResult;

} // namespace icad::exchange
