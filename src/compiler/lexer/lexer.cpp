#include "icad/compiler/lexer/lexer.hpp"

#include <cctype>
#include <string>

namespace icad::compiler {
namespace {

[[nodiscard]] auto is_identifier_start(char value) -> bool {
    const auto character = static_cast<unsigned char>(value);
    return std::isalpha(character) != 0 || value == '_';
}

[[nodiscard]] auto is_identifier_continue(char value) -> bool {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 || value == '_';
}

} // namespace

auto lex(std::string_view source) -> LexResult {
    LexResult result;
    std::size_t cursor = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    const auto add_token = [&](TokenKind kind, std::size_t start, std::size_t length,
                               SourceLocation location) {
        result.tokens.push_back(Token{kind, std::string{source.substr(start, length)}, location});
    };

    while (cursor < source.size()) {
        const char current = source[cursor];

        if (current == ' ' || current == '\t' || current == '\r') {
            ++cursor;
            ++column;
            continue;
        }

        if (current == '\n') {
            add_token(TokenKind::newline, cursor, 1, SourceLocation{line, column});
            ++cursor;
            ++line;
            column = 1;
            continue;
        }

        const bool hash_comment = current == '#';
        const bool slash_comment =
            current == '/' && cursor + 1 < source.size() && source[cursor + 1] == '/';
        if (hash_comment || slash_comment) {
            while (cursor < source.size() && source[cursor] != '\n') {
                ++cursor;
                ++column;
            }
            continue;
        }

        const SourceLocation location{line, column};
        if (is_identifier_start(current)) {
            const std::size_t start = cursor;
            while (cursor < source.size() && is_identifier_continue(source[cursor])) {
                ++cursor;
                ++column;
            }
            add_token(TokenKind::identifier, start, cursor - start, location);
            continue;
        }

        const bool negative_number = current == '-' && cursor + 1 < source.size() &&
                                     std::isdigit(static_cast<unsigned char>(source[cursor + 1])) != 0;
        if (std::isdigit(static_cast<unsigned char>(current)) != 0 || negative_number) {
            const std::size_t start = cursor;
            bool has_decimal_point = false;
            if (negative_number) {
                ++cursor;
                ++column;
            }
            while (cursor < source.size()) {
                const char candidate = source[cursor];
                if (std::isdigit(static_cast<unsigned char>(candidate)) != 0) {
                    ++cursor;
                    ++column;
                    continue;
                }
                if (candidate == '.' && !has_decimal_point && cursor + 1 < source.size() &&
                    std::isdigit(static_cast<unsigned char>(source[cursor + 1])) != 0) {
                    has_decimal_point = true;
                    ++cursor;
                    ++column;
                    continue;
                }
                break;
            }
            add_token(TokenKind::number, start, cursor - start, location);
            continue;
        }

        add_token(TokenKind::invalid, cursor, 1, location);
        result.diagnostics.push_back(Diagnostic{
            DiagnosticSeverity::error,
            "ICAD-L0001",
            "unexpected character '" + std::string(1, current) + "'",
            location,
        });
        ++cursor;
        ++column;
    }

    result.tokens.push_back(Token{TokenKind::end_of_file, "", SourceLocation{line, column}});
    return result;
}

} // namespace icad::compiler
