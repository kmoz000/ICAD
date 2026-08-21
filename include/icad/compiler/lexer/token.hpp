#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <string>

namespace icad::compiler {

enum class TokenKind { identifier, number, newline, end_of_file, invalid };

struct Token {
    TokenKind kind{TokenKind::invalid};
    std::string lexeme;
    SourceLocation location;
};

[[nodiscard]] constexpr auto token_kind_name(TokenKind kind) -> const char* {
    switch (kind) {
    case TokenKind::identifier:
        return "identifier";
    case TokenKind::number:
        return "number";
    case TokenKind::newline:
        return "newline";
    case TokenKind::end_of_file:
        return "end-of-file";
    case TokenKind::invalid:
        return "invalid";
    }
    return "invalid";
}

} // namespace icad::compiler
