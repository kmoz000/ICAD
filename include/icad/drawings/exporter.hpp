#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <filesystem>
#include <string>

namespace icad::drawings {

struct ExportResult {
    bool success{false};
    std::string message;
};

[[nodiscard]] auto write_svg(const compiler::ir::Project& project,
                             const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto write_dxf(const compiler::ir::Project& project,
                             const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto inspect_dxf(const std::filesystem::path& input) -> ExportResult;

} // namespace icad::drawings
