#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace icad::compiler {

enum class TokenKind : std::uint8_t {
    identifier,
    number,
    string_literal,
    comment,
    newline,
    dot,
    comma,
    colon,
    plus,
    minus,
    star,
    slash,
    left_parenthesis,
    right_parenthesis,
    left_bracket,
    right_bracket,
    end_of_file,
    invalid,
};

struct Token {
    TokenKind kind{TokenKind::invalid};
    std::string lexeme;
    SourceLocation location;
    std::size_t byte_offset{0};
    std::size_t byte_length{0};
    SourceLocation end_location;
};

[[nodiscard]] constexpr auto token_kind_name(TokenKind kind) -> const char* {
    switch (kind) {
    case TokenKind::identifier:
        return "identifier";
    case TokenKind::number:
        return "number";
    case TokenKind::string_literal:
        return "string";
    case TokenKind::comment:
        return "comment";
    case TokenKind::newline:
        return "newline";
    case TokenKind::dot:
        return "dot";
    case TokenKind::comma:
        return "comma";
    case TokenKind::colon:
        return "colon";
    case TokenKind::plus:
        return "plus";
    case TokenKind::minus:
        return "minus";
    case TokenKind::star:
        return "star";
    case TokenKind::slash:
        return "slash";
    case TokenKind::left_parenthesis:
        return "left-parenthesis";
    case TokenKind::right_parenthesis:
        return "right-parenthesis";
    case TokenKind::left_bracket:
        return "left-bracket";
    case TokenKind::right_bracket:
        return "right-bracket";
    case TokenKind::end_of_file:
        return "end-of-file";
    case TokenKind::invalid:
        return "invalid";
    }
    return "invalid";
}

} // namespace icad::compiler
