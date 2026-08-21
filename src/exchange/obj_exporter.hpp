#pragma once

#include "icad/exchange/exporter.hpp"

namespace icad::exchange {

[[nodiscard]] auto write_obj(const compiler::ir::Project& project,
                             const std::filesystem::path& output) -> ExportResult;

} // namespace icad::exchange

