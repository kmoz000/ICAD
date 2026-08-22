#include "icad/compiler/lexer/lexer.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

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

[[nodiscard]] auto is_decimal_digit(char value) -> bool {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

[[nodiscard]] auto is_hex_digit(char value) -> bool {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

[[nodiscard]] auto is_simple_escape(char value) -> bool {
    constexpr std::string_view escapes{"\"\\/bfnrt"};
    return escapes.find(value) != std::string_view::npos;
}

[[nodiscard]] auto punctuation_kind(char value) -> TokenKind {
    switch (value) {
    case '.':
        return TokenKind::dot;
    case ',':
        return TokenKind::comma;
    case ':':
        return TokenKind::colon;
    case '+':
        return TokenKind::plus;
    case '-':
        return TokenKind::minus;
    case '*':
        return TokenKind::star;
    case '/':
        return TokenKind::slash;
    case '(':
        return TokenKind::left_parenthesis;
    case ')':
        return TokenKind::right_parenthesis;
    case '[':
        return TokenKind::left_bracket;
    case ']':
        return TokenKind::right_bracket;
    default:
        return TokenKind::invalid;
    }
}

[[nodiscard]] auto escaped_character(char value) -> std::string {
    switch (value) {
    case '\0':
        return "\\0";
    case '\a':
        return "\\a";
    case '\b':
        return "\\b";
    case '\t':
        return "\\t";
    case '\v':
        return "\\v";
    case '\f':
        return "\\f";
    default:
        return std::string(1, value);
    }
}

} // namespace

auto lex(std::string_view source) -> LexResult {
    LexResult result;
    std::size_t cursor = 0;
    std::size_t line = 1;
    std::size_t column = 1;

    const auto add_token = [&](TokenKind kind, std::size_t start, std::size_t length,
                               SourceLocation location, SourceLocation end_location) {
        result.tokens.push_back(Token{kind,
                                      std::string{source.substr(start, length)},
                                      location,
                                      start,
                                      length,
                                      end_location});
    };
    const auto add_diagnostic = [&](std::string code, std::string message,
                                    SourceLocation location) {
        result.diagnostics.push_back(Diagnostic{
            DiagnosticSeverity::error,
            std::move(code),
            std::move(message),
            location,
        });
    };

    while (cursor < source.size()) {
        const char current = source[cursor];

        if (current == ' ' || current == '\t') {
            ++cursor;
            ++column;
            continue;
        }

        if (current == '\r' || current == '\n') {
            const std::size_t start = cursor;
            const SourceLocation location{line, column};
            if (current == '\r' && cursor + 1 < source.size() && source[cursor + 1] == '\n') {
                cursor += 2;
            } else {
                ++cursor;
            }
            ++line;
            column = 1;
            add_token(TokenKind::newline, start, cursor - start, location,
                      SourceLocation{line, column});
            continue;
        }

        const bool hash_comment = current == '#';
        const bool slash_comment =
            current == '/' && cursor + 1 < source.size() && source[cursor + 1] == '/';
        if (hash_comment || slash_comment) {
            const std::size_t start = cursor;
            const SourceLocation location{line, column};
            while (cursor < source.size() && source[cursor] != '\r' && source[cursor] != '\n') {
                ++cursor;
                ++column;
            }
            add_token(TokenKind::comment, start, cursor - start, location,
                      SourceLocation{line, column});
            continue;
        }

        const SourceLocation location{line, column};
        if (is_identifier_start(current)) {
            const std::size_t start = cursor;
            while (cursor < source.size() && is_identifier_continue(source[cursor])) {
                ++cursor;
                ++column;
            }
            add_token(TokenKind::identifier, start, cursor - start, location,
                      SourceLocation{line, column});
            continue;
        }

        const bool leading_decimal = current == '.' && cursor + 1 < source.size() &&
                                     is_decimal_digit(source[cursor + 1]);
        const bool negative_number = current == '-' && cursor + 1 < source.size() &&
                                     (is_decimal_digit(source[cursor + 1]) ||
                                      (source[cursor + 1] == '.' && cursor + 2 < source.size() &&
                                       is_decimal_digit(source[cursor + 2])));
        if (is_decimal_digit(current) || leading_decimal || negative_number) {
            const std::size_t start = cursor;
            if (negative_number) {
                ++cursor;
                ++column;
            }

            while (cursor < source.size() && is_decimal_digit(source[cursor])) {
                ++cursor;
                ++column;
            }
            if (cursor + 1 < source.size() && source[cursor] == '.' &&
                is_decimal_digit(source[cursor + 1])) {
                ++cursor;
                ++column;
                while (cursor < source.size() && is_decimal_digit(source[cursor])) {
                    ++cursor;
                    ++column;
                }
            }

            if (cursor < source.size() && (source[cursor] == 'e' || source[cursor] == 'E')) {
                const std::size_t exponent_start = cursor;
                ++cursor;
                ++column;
                if (cursor < source.size() && (source[cursor] == '+' || source[cursor] == '-')) {
                    ++cursor;
                    ++column;
                }
                const std::size_t exponent_digits = cursor;
                while (cursor < source.size() && is_decimal_digit(source[cursor])) {
                    ++cursor;
                    ++column;
                }
                if (cursor == exponent_digits) {
                    add_diagnostic("ICAD-L0004", "numeric exponent requires at least one digit",
                                   SourceLocation{line, location.column + exponent_start - start});
                }
            }

            add_token(TokenKind::number, start, cursor - start, location,
                      SourceLocation{line, column});
            continue;
        }

        if (current == '"') {
            const std::size_t start = cursor;
            ++cursor;
            ++column;
            bool terminated = false;
            while (cursor < source.size() && source[cursor] != '\r' && source[cursor] != '\n') {
                if (source[cursor] == '"') {
                    ++cursor;
                    ++column;
                    terminated = true;
                    break;
                }
                if (source[cursor] != '\\') {
                    const auto character = static_cast<unsigned char>(source[cursor]);
                    if (character < 0x20U) {
                        add_diagnostic("ICAD-L0003", "control character is not allowed in a string",
                                       SourceLocation{line, column});
                    }
                    ++cursor;
                    ++column;
                    continue;
                }

                const SourceLocation escape_location{line, column};
                ++cursor;
                ++column;
                if (cursor >= source.size() || source[cursor] == '\r' || source[cursor] == '\n') {
                    add_diagnostic("ICAD-L0003", "incomplete string escape", escape_location);
                    break;
                }
                if (is_simple_escape(source[cursor])) {
                    ++cursor;
                    ++column;
                    continue;
                }
                if (source[cursor] == 'u') {
                    ++cursor;
                    ++column;
                    std::size_t digits = 0;
                    while (digits < 4 && cursor < source.size() && is_hex_digit(source[cursor])) {
                        ++cursor;
                        ++column;
                        ++digits;
                    }
                    if (digits != 4) {
                        add_diagnostic("ICAD-L0003",
                                       "Unicode string escape requires four hexadecimal digits",
                                       escape_location);
                    }
                    continue;
                }
                add_diagnostic("ICAD-L0003",
                               "unsupported string escape '\\" +
                                   escaped_character(source[cursor]) + "'",
                               escape_location);
                ++cursor;
                ++column;
            }
            if (!terminated) {
                add_diagnostic("ICAD-L0002", "unterminated string literal", location);
            }
            add_token(TokenKind::string_literal, start, cursor - start, location,
                      SourceLocation{line, column});
            continue;
        }

        const TokenKind punctuation = punctuation_kind(current);
        if (punctuation != TokenKind::invalid) {
            add_token(punctuation, cursor, 1, location, SourceLocation{line, column + 1});
            ++cursor;
            ++column;
            continue;
        }

        add_token(TokenKind::invalid, cursor, 1, location, SourceLocation{line, column + 1});
        add_diagnostic("ICAD-L0001",
                       "unexpected character '" + escaped_character(current) + "'", location);
        ++cursor;
        ++column;
    }

    result.tokens.push_back(Token{TokenKind::end_of_file,
                                  "",
                                  SourceLocation{line, column},
                                  cursor,
                                  0,
                                  SourceLocation{line, column}});
    return result;
}

} // namespace icad::compiler
