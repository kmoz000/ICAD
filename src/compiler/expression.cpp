#include "icad/compiler/expression.hpp"

#include <cmath>
#include <cstddef>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace icad::compiler {
namespace {

using Operation = ast::ScalarExpressionOp;

auto add_error(std::vector<Diagnostic>& diagnostics, std::string code, std::string message,
               SourceLocation location) -> void {
    diagnostics.push_back(
        Diagnostic{DiagnosticSeverity::error, std::move(code), std::move(message), location});
}

[[nodiscard]] auto parse_number(const Token& token, double& value) -> bool {
    std::istringstream stream{token.lexeme};
    stream.imbue(std::locale::classic());
    if (!(stream >> value))
        return false;
    char trailing{};
    return !(stream >> trailing);
}

[[nodiscard]] auto render_source(std::span<const Token> tokens) -> std::string {
    std::string source;
    TokenKind previous = TokenKind::invalid;
    for (const auto& token : tokens) {
        const bool tight = token.kind == TokenKind::dot || previous == TokenKind::dot ||
                           token.kind == TokenKind::right_parenthesis ||
                           previous == TokenKind::left_parenthesis;
        if (!source.empty() && !tight) {
            source.push_back(' ');
        }
        source += token.lexeme;
        previous = token.kind;
    }
    return source;
}

class ExpressionParser {
  public:
    explicit ExpressionParser(std::span<const Token> tokens) : tokens_(tokens) {
        result_.expression.source = render_source(tokens);
        if (!tokens.empty()) {
            result_.expression.location = tokens.front().location;
        }
    }

    [[nodiscard]] auto parse() -> ExpressionParseResult {
        if (tokens_.empty()) {
            add_error(result_.diagnostics, "ICAD-E0001", "expected a scalar expression", {1, 1});
            return std::move(result_);
        }
        parse_additive();
        if (index_ != tokens_.size()) {
            add_error(result_.diagnostics, "ICAD-E0001",
                      "unexpected token '" + tokens_[index_].lexeme + "' in scalar expression",
                      tokens_[index_].location);
        }
        return std::move(result_);
    }

  private:
    auto push_operator(Operation operation, SourceLocation location) -> void {
        result_.expression.postfix.push_back({operation, 0.0, {}, {}, location});
    }

    auto parse_additive() -> void {
        parse_multiplicative();
        while (index_ < tokens_.size()) {
            const Token& token = tokens_[index_];
            if (token.kind == TokenKind::plus || token.kind == TokenKind::minus) {
                ++index_;
                parse_multiplicative();
                push_operator(token.kind == TokenKind::plus ? Operation::add : Operation::subtract,
                              token.location);
                continue;
            }
            // Signed numeric literals remain one token for v1 source compatibility.
            // At additive position, a negative literal is equivalent to `+ -literal`.
            if (token.kind == TokenKind::number && token.lexeme.starts_with('-')) {
                parse_multiplicative();
                push_operator(Operation::add, token.location);
                continue;
            }
            break;
        }
    }

    auto parse_multiplicative() -> void {
        parse_unary();
        while (index_ < tokens_.size() &&
               (tokens_[index_].kind == TokenKind::star ||
                tokens_[index_].kind == TokenKind::slash)) {
            const Token operation = tokens_[index_++];
            parse_unary();
            push_operator(operation.kind == TokenKind::star ? Operation::multiply
                                                            : Operation::divide,
                          operation.location);
        }
    }

    auto parse_unary() -> void {
        if (index_ < tokens_.size() &&
            (tokens_[index_].kind == TokenKind::plus ||
             tokens_[index_].kind == TokenKind::minus)) {
            const Token operation = tokens_[index_++];
            parse_unary();
            push_operator(operation.kind == TokenKind::plus ? Operation::unary_plus
                                                            : Operation::unary_minus,
                          operation.location);
            return;
        }
        parse_primary();
    }

    auto parse_primary() -> void {
        if (index_ >= tokens_.size()) {
            const SourceLocation location = tokens_.empty() ? SourceLocation{1, 1}
                                                            : tokens_.back().end_location;
            add_error(result_.diagnostics, "ICAD-E0001", "expected an expression operand",
                      location);
            return;
        }

        const Token& token = tokens_[index_];
        if (token.kind == TokenKind::number) {
            double number = 0.0;
            if (!parse_number(token, number)) {
                add_error(result_.diagnostics, "ICAD-E0001", "invalid numeric literal",
                          token.location);
            }
            ++index_;
            std::string unit;
            if (index_ < tokens_.size() && tokens_[index_].kind == TokenKind::identifier) {
                unit = tokens_[index_++].lexeme;
            }
            result_.expression.postfix.push_back(
                {Operation::literal, number, {}, std::move(unit), token.location});
            return;
        }

        if (token.kind == TokenKind::identifier) {
            std::string reference = token.lexeme;
            const SourceLocation location = token.location;
            ++index_;
            while (index_ < tokens_.size() && tokens_[index_].kind == TokenKind::dot) {
                ++index_;
                if (index_ >= tokens_.size() ||
                    tokens_[index_].kind != TokenKind::identifier) {
                    const SourceLocation error_location =
                        index_ < tokens_.size() ? tokens_[index_].location
                                               : tokens_.back().end_location;
                    add_error(result_.diagnostics, "ICAD-E0001",
                              "qualified name expects an identifier after '.'", error_location);
                    break;
                }
                reference.push_back('.');
                reference += tokens_[index_++].lexeme;
            }
            result_.expression.references.push_back(reference);
            result_.expression.postfix.push_back(
                {Operation::reference, 0.0, std::move(reference), {}, location});
            return;
        }

        if (token.kind == TokenKind::left_parenthesis) {
            ++index_;
            parse_additive();
            if (index_ >= tokens_.size() ||
                tokens_[index_].kind != TokenKind::right_parenthesis) {
                add_error(result_.diagnostics, "ICAD-E0001",
                          "scalar expression is missing ')'", token.location);
                return;
            }
            ++index_;
            return;
        }

        add_error(result_.diagnostics, "ICAD-E0001",
                  "expected a number, qualified name, or parenthesized expression",
                  token.location);
        ++index_;
    }

    std::span<const Token> tokens_;
    std::size_t index_{};
    ExpressionParseResult result_;
};

[[nodiscard]] auto canonical_literal(const ast::ScalarExpressionNode& node,
                                     std::vector<Diagnostic>& diagnostics)
    -> std::optional<EvaluatedScalar> {
    if (node.unit.empty()) {
        return EvaluatedScalar{node.literal, units::Dimension::dimensionless};
    }
    const auto definition = units::find(node.unit);
    if (!definition) {
        add_error(diagnostics, "ICAD-E0003", "unknown unit '" + node.unit + "'", node.location);
        return std::nullopt;
    }
    return EvaluatedScalar{node.literal * definition->scale_to_canonical,
                           definition->dimension};
}

auto evaluate_binary(const ast::ScalarExpressionNode& node, EvaluatedScalar left,
                     EvaluatedScalar right, std::vector<Diagnostic>& diagnostics)
    -> std::optional<EvaluatedScalar> {
    if (node.operation == Operation::add || node.operation == Operation::subtract) {
        if (left.dimension != right.dimension) {
            add_error(diagnostics, "ICAD-E0004",
                      "addition and subtraction require matching physical dimensions",
                      node.location);
            return std::nullopt;
        }
        return EvaluatedScalar{node.operation == Operation::add ? left.value + right.value
                                                                : left.value - right.value,
                               left.dimension};
    }
    if (node.operation == Operation::multiply) {
        if (left.dimension == units::Dimension::dimensionless) {
            return EvaluatedScalar{left.value * right.value, right.dimension};
        }
        if (right.dimension == units::Dimension::dimensionless) {
            return EvaluatedScalar{left.value * right.value, left.dimension};
        }
        add_error(diagnostics, "ICAD-E0005",
                  "v1 scalar expressions require one multiplication operand to be dimensionless",
                  node.location);
        return std::nullopt;
    }
    if (std::abs(right.value) <= 1.0e-15) {
        add_error(diagnostics, "ICAD-E0006", "division by zero in scalar expression",
                  node.location);
        return std::nullopt;
    }
    if (right.dimension == units::Dimension::dimensionless) {
        return EvaluatedScalar{left.value / right.value, left.dimension};
    }
    if (left.dimension == right.dimension) {
        return EvaluatedScalar{left.value / right.value, units::Dimension::dimensionless};
    }
    add_error(diagnostics, "ICAD-E0005",
              "v1 scalar expressions cannot represent this derived physical dimension",
              node.location);
    return std::nullopt;
}

} // namespace

auto parse_scalar_expression(std::span<const Token> tokens) -> ExpressionParseResult {
    return ExpressionParser{tokens}.parse();
}

auto evaluate_scalar_expression(const ast::ScalarExpression& expression,
                                const ScalarLookup& lookup) -> ExpressionEvaluationResult {
    ExpressionEvaluationResult result;
    std::vector<EvaluatedScalar> stack;
    for (const auto& node : expression.postfix) {
        if (node.operation == Operation::literal) {
            if (auto literal = canonical_literal(node, result.diagnostics)) {
                stack.push_back(*literal);
            }
            continue;
        }
        if (node.operation == Operation::reference) {
            const auto value = lookup(node.symbol);
            if (!value) {
                add_error(result.diagnostics, "ICAD-E0002",
                          "unknown scalar reference '" + node.symbol + "'", node.location);
            } else {
                stack.push_back(*value);
            }
            continue;
        }
        if (node.operation == Operation::unary_plus ||
            node.operation == Operation::unary_minus) {
            if (stack.empty()) {
                add_error(result.diagnostics, "ICAD-E0001", "invalid unary expression",
                          node.location);
                continue;
            }
            if (node.operation == Operation::unary_minus) {
                stack.back().value = -stack.back().value;
            }
            continue;
        }
        if (stack.size() < 2) {
            add_error(result.diagnostics, "ICAD-E0001", "invalid binary expression",
                      node.location);
            continue;
        }
        const EvaluatedScalar right = stack.back();
        stack.pop_back();
        const EvaluatedScalar left = stack.back();
        stack.pop_back();
        if (auto value = evaluate_binary(node, left, right, result.diagnostics)) {
            stack.push_back(*value);
        }
    }
    if (result.diagnostics.empty() && stack.size() == 1) {
        result.value = stack.back();
    } else if (result.diagnostics.empty()) {
        add_error(result.diagnostics, "ICAD-E0001", "invalid scalar expression",
                  expression.location);
    }
    return result;
}

} // namespace icad::compiler
