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

// Lexes one immutable source snapshot. Tokens retain their spelling for the
// current parser and also carry zero-based byte spans plus one-based source
// locations for editor and incremental-compiler consumers.
[[nodiscard]] auto lex(std::string_view source) -> LexResult;

} // namespace icad::compiler
