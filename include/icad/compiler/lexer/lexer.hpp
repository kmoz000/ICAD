#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <string_view>
#include <vector>

namespace icad::compiler {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

[[nodiscard]] auto lex(std::string_view source) -> LexResult;

} // namespace icad::compiler
