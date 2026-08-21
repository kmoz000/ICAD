#pragma once

#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <string>
#include <vector>

namespace icad::compiler {

enum class SymbolKind {
    parameter,
    material,
    profile,
    sketch,
    sketch_point,
    sketch_constraint,
    instance,
    body,
    feature,
    constraint,
    scene,
    animation_track
};

struct Symbol {
    SymbolKind kind{SymbolKind::parameter};
    std::string name;
    std::string scope;
    SourceLocation location;
};

struct ResolveResult {
    std::vector<Symbol> symbols;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

[[nodiscard]] auto resolve(const ast::Program& program) -> ResolveResult;

} // namespace icad::compiler
