#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <cstddef>
#include <iostream>
#include <string_view>

namespace {

auto require(bool condition, std::string_view message) -> bool {
    if (!condition) {
        std::cerr << "lexer test failure: " << message << '\n';
    }
    return condition;
}

auto require_token(const icad::compiler::LexResult& result, std::size_t index,
                   icad::compiler::TokenKind kind, std::string_view lexeme,
                   std::string_view message) -> bool {
    if (index >= result.tokens.size()) {
        std::cerr << "lexer test failure: " << message << " (missing token " << index << ")\n";
        return false;
    }
    const auto& token = result.tokens[index];
    return require(token.kind == kind && token.lexeme == lexeme, message);
}

} // namespace

auto main() -> int {
    using icad::compiler::TokenKind;

    bool passed = true;

    const std::string_view compatible_source =
        "# comment\n"
        "PROJECT demo_1\n"
        "WIDTH 80.5 mm // trailing comment\n"
        "POSITION -12.5 mm\n";
    const auto compatible = icad::compiler::lex(compatible_source);

    passed &= require(compatible.ok(), "current source has no diagnostics");
    passed &= require(compatible.tokens.size() == 15,
                      "current source retains comments and structural tokens");
    passed &= require_token(compatible, 0, TokenKind::comment, "# comment",
                            "hash comment is retained");
    passed &= require_token(compatible, 2, TokenKind::identifier, "PROJECT",
                            "PROJECT remains an identifier");
    passed &= require_token(compatible, 6, TokenKind::number, "80.5",
                            "decimal value remains a number");
    passed &= require_token(compatible, 8, TokenKind::comment, "// trailing comment",
                            "slash comment is retained");
    passed &= require_token(compatible, 11, TokenKind::number, "-12.5",
                            "legacy negative value remains one number");
    passed &= require(compatible.tokens[2].location.line == 2 &&
                          compatible.tokens[2].location.column == 1,
                      "token start location is one-based");
    passed &= require(compatible.tokens[2].byte_offset == 10 &&
                          compatible.tokens[2].byte_length == 7,
                      "token byte span identifies its exact source spelling");
    passed &= require(compatible.tokens[2].end_location.line == 2 &&
                          compatible.tokens[2].end_location.column == 8,
                      "token end location is exclusive");
    passed &= require(compatible.tokens.back().kind == TokenKind::end_of_file &&
                          compatible.tokens.back().byte_offset == compatible_source.size() &&
                          compatible.tokens.back().byte_length == 0,
                      "EOF is a zero-length span at the source end");

    const auto proposed_lexical_surface = icad::compiler::lex(
        "SELECT body.feature[ROLE HOLE] + (2 * width) / 4, \"blue\\n\":.5 1.2e-4 -\n");
    passed &= require(proposed_lexical_surface.ok(),
                      "v2 punctuation, string, and number spellings lex cleanly");
    passed &= require_token(proposed_lexical_surface, 2, TokenKind::dot, ".",
                            "qualified-name dot is explicit");
    passed &= require_token(proposed_lexical_surface, 4, TokenKind::left_bracket, "[",
                            "selector opening bracket is explicit");
    passed &= require_token(proposed_lexical_surface, 7, TokenKind::right_bracket, "]",
                            "selector closing bracket is explicit");
    passed &= require_token(proposed_lexical_surface, 8, TokenKind::plus, "+",
                            "addition operator is explicit");
    passed &= require_token(proposed_lexical_surface, 9, TokenKind::left_parenthesis, "(",
                            "opening parenthesis is explicit");
    passed &= require_token(proposed_lexical_surface, 11, TokenKind::star, "*",
                            "multiplication operator is explicit");
    passed &= require_token(proposed_lexical_surface, 13, TokenKind::right_parenthesis, ")",
                            "closing parenthesis is explicit");
    passed &= require_token(proposed_lexical_surface, 14, TokenKind::slash, "/",
                            "division operator is distinct from a comment");
    passed &= require_token(proposed_lexical_surface, 16, TokenKind::comma, ",",
                            "comma is explicit");
    passed &= require_token(proposed_lexical_surface, 17, TokenKind::string_literal,
                            "\"blue\\n\"", "string spelling and escapes are retained");
    passed &= require_token(proposed_lexical_surface, 18, TokenKind::colon, ":",
                            "colon is explicit");
    passed &= require_token(proposed_lexical_surface, 19, TokenKind::number, ".5",
                            "leading-decimal number is accepted");
    passed &= require_token(proposed_lexical_surface, 20, TokenKind::number, "1.2e-4",
                            "scientific notation is one number");
    passed &= require_token(proposed_lexical_surface, 21, TokenKind::minus, "-",
                            "standalone subtraction operator is explicit");

    const auto negative_scientific = icad::compiler::lex("VALUE -1.2e-4 mm\n");
    passed &= require(negative_scientific.ok(), "legacy signed scientific number lexes cleanly");
    passed &= require_token(negative_scientific, 1, TokenKind::number, "-1.2e-4",
                            "legacy signed scientific number remains one token");

    const auto line_endings = icad::compiler::lex("A\r\nB\rC\n");
    passed &= require(line_endings.ok(), "CRLF, CR, and LF line endings lex cleanly");
    passed &= require_token(line_endings, 1, TokenKind::newline, "\r\n",
                            "CRLF is one newline token");
    passed &= require_token(line_endings, 3, TokenKind::newline, "\r",
                            "CR is one newline token");
    passed &= require(line_endings.tokens[2].location.line == 2 &&
                          line_endings.tokens[4].location.line == 3 &&
                          line_endings.tokens.back().location.line == 4,
                      "all supported line endings advance source lines once");

    const auto unexpected = icad::compiler::lex("WIDTH @ mm\n");
    passed &= require(!unexpected.ok(), "invalid punctuation produces a diagnostic");
    passed &= require(unexpected.diagnostics.size() == 1,
                      "one invalid character gives one diagnostic");
    passed &= require(unexpected.diagnostics.front().code == "ICAD-L0001",
                      "unexpected-character diagnostic code is stable");
    passed &= require(unexpected.diagnostics.front().location.column == 7,
                      "unexpected-character diagnostic column is one-based");

    const auto invalid_escape = icad::compiler::lex("NAME \"bad\\q\"\n");
    passed &= require(!invalid_escape.ok(), "invalid string escape is rejected");
    passed &= require(invalid_escape.diagnostics.size() == 1 &&
                          invalid_escape.diagnostics.front().code == "ICAD-L0003",
                      "invalid string escape has a stable diagnostic");
    passed &= require_token(invalid_escape, 1, TokenKind::string_literal, "\"bad\\q\"",
                            "invalid string remains one recoverable token");

    const auto unterminated = icad::compiler::lex("NAME \"open\nNEXT ok\n");
    passed &= require(!unterminated.ok(), "unterminated string is rejected");
    passed &= require(unterminated.diagnostics.size() == 1 &&
                          unterminated.diagnostics.front().code == "ICAD-L0002",
                      "unterminated string has one stable diagnostic");
    passed &= require_token(unterminated, 1, TokenKind::string_literal, "\"open",
                            "unterminated string stops before the newline");
    passed &= require_token(unterminated, 3, TokenKind::identifier, "NEXT",
                            "lexing recovers on the following line");

    const auto malformed_exponent = icad::compiler::lex("VALUE 1e+ mm\n");
    passed &= require(!malformed_exponent.ok(), "malformed exponent is rejected");
    passed &= require(malformed_exponent.diagnostics.size() == 1 &&
                          malformed_exponent.diagnostics.front().code == "ICAD-L0004",
                      "malformed exponent has one stable diagnostic");
    passed &= require_token(malformed_exponent, 1, TokenKind::number, "1e+",
                            "malformed exponent is retained as one recoverable token");

    return passed ? 0 : 1;
}
