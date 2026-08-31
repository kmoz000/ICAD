#pragma once

#include "icad/exchange/exporter.hpp"

namespace icad::exchange {

[[nodiscard]] auto write_stl(const compiler::ir::Project& project,
                             const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto write_stl(const compiler::ir::Project& project, const cad::Model& model,
                             const std::filesystem::path& output) -> ExportResult;

} // namespace icad::exchange
