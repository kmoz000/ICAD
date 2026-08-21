#pragma once

#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <vector>

namespace icad::compiler {

struct ParseResult {
    ast::Program program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

[[nodiscard]] auto parse(const std::vector<Token>& tokens) -> ParseResult;

} // namespace icad::compiler

