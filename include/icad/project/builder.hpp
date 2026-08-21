#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace icad::project {

struct Artifact {
    std::string kind;
    std::string media_type;
    std::filesystem::path path;
    std::uintmax_t bytes{};
};

struct BuildResult {
    bool success{false};
    std::string message;
    std::size_t components{};
    std::size_t solids{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::size_t topology_vertices{};
    std::size_t topology_edges{};
    std::size_t topology_faces{};
    std::size_t materials{};
    std::size_t scenes{};
    std::size_t keyframes{};
    std::vector<Artifact> artifacts;
};

[[nodiscard]] auto build(const compiler::ir::Project& project,
                         const std::filesystem::path& output_directory, std::string_view model_name)
    -> BuildResult;

} // namespace icad::project
