#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <iostream>
#include <string_view>

namespace {

auto require(bool condition, std::string_view message) -> bool {
    if (!condition) {
        std::cerr << "lexer test failure: " << message << '\n';
    }
    return condition;
}

} // namespace

auto main() -> int {
    using icad::compiler::TokenKind;

    const auto valid = icad::compiler::lex(
        "# comment\n"
        "PROJECT demo_1\n"
        "WIDTH 80.5 mm // trailing comment\n"
        "POSITION -12.5 mm\n");

    bool passed = true;
    passed &= require(valid.ok(), "valid source has no diagnostics");
    passed &= require(valid.tokens.size() == 13, "valid source has the expected token count");
    passed &= require(valid.tokens[1].kind == TokenKind::identifier, "PROJECT is an identifier");
    passed &= require(valid.tokens[1].lexeme == "PROJECT", "PROJECT lexeme is retained");
    passed &= require(valid.tokens[5].kind == TokenKind::number, "decimal value is a number");
    passed &= require(valid.tokens[5].lexeme == "80.5", "decimal lexeme is retained");
    passed &= require(valid.tokens[9].lexeme == "-12.5", "negative number is retained");
    passed &= require(valid.tokens.back().kind == TokenKind::end_of_file, "stream ends with EOF");

    const auto invalid = icad::compiler::lex("WIDTH @ mm\n");
    passed &= require(!invalid.ok(), "invalid punctuation produces a diagnostic");
    passed &= require(invalid.diagnostics.size() == 1, "one invalid character gives one diagnostic");
    passed &= require(invalid.diagnostics.front().code == "ICAD-L0001", "diagnostic code is stable");
    passed &= require(invalid.diagnostics.front().location.column == 7,
                      "diagnostic column is one-based");

    return passed ? 0 : 1;
}
