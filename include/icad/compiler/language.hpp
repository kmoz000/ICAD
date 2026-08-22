#pragma once

#include "icad/compiler/ast/ast.hpp"
#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/lexer/token.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace icad::compiler::language {

inline constexpr std::size_t version_major = 1;
inline constexpr std::size_t version_minor = 0;
inline constexpr std::string_view version = "1.0";

struct RequirementResult {
    std::vector<ast::RequirementDecl> requirements;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const noexcept -> bool { return diagnostics.empty(); }
};

[[nodiscard]] auto capabilities() noexcept -> std::span<const std::string_view>;
[[nodiscard]] auto supports_capability(std::string_view capability) noexcept -> bool;

// Requirements are checked before normal parsing. This prevents unsupported
// future syntax from producing a misleading cascade of parser diagnostics.
[[nodiscard]] auto check_requirements(const std::vector<Token>& tokens) -> RequirementResult;

} // namespace icad::compiler::language
