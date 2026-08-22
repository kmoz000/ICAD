#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace icad::cad {
struct Model;
}

namespace icad::scene {

struct ExportResult {
    bool success{false};
    std::string message;
    std::size_t materials{};
    std::size_t scenes{};
    std::size_t tracks{};
    std::size_t keyframes{};
};

[[nodiscard]] auto export_web_bundle(const compiler::ir::Project& project,
                                     const std::filesystem::path& output_base) -> ExportResult;
[[nodiscard]] auto export_web_bundle(const compiler::ir::Project& project,
                                     const cad::Model& model,
                                     const std::filesystem::path& output_base) -> ExportResult;
[[nodiscard]] auto web_model_json(const compiler::ir::Project& project,
                                  const cad::Model& model,
                                  std::string_view basename = "preview") -> std::string;

} // namespace icad::scene
