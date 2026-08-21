#pragma once

#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <optional>
#include <vector>

namespace icad::compiler {

struct SemanticResult {
    std::optional<ir::Project> project;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool {
        return project.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] auto analyze(const ast::Program& program) -> SemanticResult;

} // namespace icad::compiler

