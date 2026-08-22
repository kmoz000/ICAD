#pragma once

#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/lexer/token.hpp"
#include "icad/compiler/units/units.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace icad::compiler {

struct ExpressionParseResult {
    ast::ScalarExpression expression;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

struct EvaluatedScalar {
    double value{};
    units::Dimension dimension{units::Dimension::dimensionless};
};

using ScalarLookup =
    std::function<std::optional<EvaluatedScalar>(std::string_view reference)>;

struct ExpressionEvaluationResult {
    std::optional<EvaluatedScalar> value;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool {
        return value.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] auto parse_scalar_expression(std::span<const Token> tokens)
    -> ExpressionParseResult;
[[nodiscard]] auto evaluate_scalar_expression(const ast::ScalarExpression& expression,
                                              const ScalarLookup& lookup)
    -> ExpressionEvaluationResult;

} // namespace icad::compiler
