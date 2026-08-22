#pragma once

#include "icad/cad/topology.hpp"
#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/ir/ir.hpp"
#include "icad/compiler/importer.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace icad::compiler {

struct CompileResult {
    std::vector<Token> tokens;
    std::optional<ast::Program> program;
    std::optional<ir::Project> ir_project;
    std::optional<cad::TopologyModel> topology_model;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::filesystem::path> imported_files;

    [[nodiscard]] auto ok() const noexcept -> bool {
        return ir_project.has_value() && diagnostics.empty();
    }
};

struct CompileOptions {
    bool build_topology{true};
    ImportOptions imports;
};

[[nodiscard]] auto compile(std::string_view source, CompileOptions options = {}) -> CompileResult;

} // namespace icad::compiler
#include <filesystem>
