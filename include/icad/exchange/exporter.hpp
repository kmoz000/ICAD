#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace icad::exchange {

enum class ExportFormat { obj, stl, step, gltf, glb, three_mf, unsupported };

struct ExportResult {
    bool success{false};
    std::string message;
    std::size_t objects{};
    std::size_t vertices{};
    std::size_t triangles{};
};

struct StepInspection {
    bool success{false};
    std::string message;
    std::size_t roots{};
    std::size_t solids{};
    std::size_t assembly_components{};
};

struct StlInspection {
    bool success{false};
    std::string message;
    std::size_t solids{};
    std::size_t facets{};
};

struct MeshPackageInspection {
    bool success{false};
    std::string message;
    std::size_t objects{};
};

[[nodiscard]] auto format_from_extension(const std::filesystem::path& output)
    -> ExportFormat;
[[nodiscard]] auto export_project(const compiler::ir::Project& project,
                                  const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto inspect_step(const std::filesystem::path& input) -> StepInspection;
[[nodiscard]] auto export_assembly_step(const compiler::ir::Project& project,
                                        const std::filesystem::path& output) -> ExportResult;
[[nodiscard]] auto inspect_stl(const std::filesystem::path& input) -> StlInspection;
[[nodiscard]] auto inspect_gltf(const std::filesystem::path& input) -> MeshPackageInspection;
[[nodiscard]] auto inspect_3mf(const std::filesystem::path& input) -> MeshPackageInspection;

} // namespace icad::exchange
