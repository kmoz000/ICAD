#include "icad/compiler/compiler.hpp"

#include "icad/compiler/lexer/lexer.hpp"
#include "icad/compiler/parser/parser.hpp"
#include "icad/compiler/resolver/resolver.hpp"
#include "icad/compiler/semantic/semantic.hpp"

#include <iterator>
#include <utility>

namespace icad::compiler {
namespace {

auto append(std::vector<Diagnostic>& destination, std::vector<Diagnostic> source) -> void {
    destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                       std::make_move_iterator(source.end()));
}

} // namespace

auto compile(std::string_view source, CompileOptions options) -> CompileResult {
    CompileResult result;

    auto imported = expand_imports(source, options.imports);
    result.imported_files = std::move(imported.dependencies);
    append(result.diagnostics, std::move(imported.diagnostics));
    if (!result.diagnostics.empty())
        return result;

    auto lexed = lex(imported.source);
    result.tokens = std::move(lexed.tokens);
    append(result.diagnostics, std::move(lexed.diagnostics));
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto parsed = parse(result.tokens);
    result.program = std::move(parsed.program);
    append(result.diagnostics, std::move(parsed.diagnostics));
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto resolved = resolve(*result.program);
    append(result.diagnostics, std::move(resolved.diagnostics));
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto semantic = analyze(*result.program);
    result.ir_project = std::move(semantic.project);
    append(result.diagnostics, std::move(semantic.diagnostics));
    if (result.diagnostics.empty() && options.build_topology) {
        result.topology_model = cad::build_topology(*result.ir_project);
        if (!result.topology_model->solids.empty()) {
            const auto validation = cad::validate_topology(*result.topology_model);
            for (const auto& issue : validation.issues) {
                result.diagnostics.push_back({DiagnosticSeverity::error,
                                              issue.code,
                                              issue.message + " [" + issue.entity + "]",
                                              {1, 1}});
            }
        }
    }
    return result;
}

} // namespace icad::compiler
