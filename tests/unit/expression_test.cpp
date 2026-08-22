#include "icad/compiler/expression.hpp"
#include "icad/compiler/lexer/lexer.hpp"

#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

[[nodiscard]] auto parse(std::string_view source) -> icad::compiler::ExpressionParseResult {
    const auto lexed = icad::compiler::lex(source);
    if (!lexed.ok() || lexed.tokens.empty()) {
        return {};
    }
    return icad::compiler::parse_scalar_expression(
        std::span<const icad::compiler::Token>{lexed.tokens.data(), lexed.tokens.size() - 1});
}

auto require(bool condition, std::string_view message) -> bool {
    if (!condition) {
        std::cerr << "expression test failure: " << message << '\n';
    }
    return condition;
}

} // namespace

auto main() -> int {
    using icad::compiler::EvaluatedScalar;
    using icad::compiler::units::Dimension;

    bool passed = true;
    const auto parsed = parse("120 mm - 2 * (8 mm)");
    passed &= require(parsed.ok(), "parser accepts precedence and parentheses");
    const auto evaluated = icad::compiler::evaluate_scalar_expression(
        parsed.expression,
        [](std::string_view) -> std::optional<EvaluatedScalar> { return std::nullopt; });
    passed &= require(evaluated.ok() && evaluated.value->dimension == Dimension::length &&
                          std::abs(evaluated.value->value - 104.0) < 1.0e-9,
                      "typed evaluator preserves length and precedence");

    const auto qualified = parse("robot.width / 2");
    const auto qualified_value = icad::compiler::evaluate_scalar_expression(
        qualified.expression, [](std::string_view reference) -> std::optional<EvaluatedScalar> {
            if (reference == "robot.width") {
                return EvaluatedScalar{200.0, Dimension::length};
            }
            return std::nullopt;
        });
    passed &= require(qualified.ok() && qualified.expression.references.size() == 1 &&
                          qualified.expression.references.front() == "robot.width" &&
                          qualified_value.ok() &&
                          std::abs(qualified_value.value->value - 100.0) < 1.0e-9,
                      "qualified reference is retained and evaluated");

    const auto incompatible = parse("1 mm + 2 deg");
    const auto incompatible_value = icad::compiler::evaluate_scalar_expression(
        incompatible.expression,
        [](std::string_view) -> std::optional<EvaluatedScalar> { return std::nullopt; });
    passed &= require(!incompatible_value.ok() &&
                          incompatible_value.diagnostics.front().code == "ICAD-E0004",
                      "dimension mismatch has a stable diagnostic");

    const auto zero = parse("10 mm / (2 - 2)");
    const auto zero_value = icad::compiler::evaluate_scalar_expression(
        zero.expression,
        [](std::string_view) -> std::optional<EvaluatedScalar> { return std::nullopt; });
    passed &= require(!zero_value.ok() && zero_value.diagnostics.front().code == "ICAD-E0006",
                      "division by zero has a stable diagnostic");

    return passed ? 0 : 1;
}
