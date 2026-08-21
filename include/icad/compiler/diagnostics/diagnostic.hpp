#pragma once

#include <cstddef>
#include <string>

namespace icad::compiler {

enum class DiagnosticSeverity {
    error,
    warning,
    note,
};

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string code;
    std::string message;
    SourceLocation location;
};

} // namespace icad::compiler
