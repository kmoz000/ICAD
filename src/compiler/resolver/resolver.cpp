#include "icad/compiler/resolver/resolver.hpp"

#include <string>
#include <unordered_set>

namespace icad::compiler {
namespace {

auto add_symbol(ResolveResult& result, std::unordered_set<std::string>& names, Symbol symbol)
    -> void {
    const std::string qualified_name = symbol.scope + "::" + symbol.name;
    if (!names.insert(qualified_name).second) {
        result.diagnostics.push_back(Diagnostic{
            DiagnosticSeverity::error,
            "ICAD-R0001",
            "duplicate symbol '" + symbol.name + "' in scope '" + symbol.scope + "'",
            symbol.location,
        });
        return;
    }
    result.symbols.push_back(std::move(symbol));
}

} // namespace

auto resolve(const ast::Program& program) -> ResolveResult {
    ResolveResult result;
    std::unordered_set<std::string> names;
    for (const auto& parameter : program.parameters) {
        add_symbol(result, names,
                   Symbol{SymbolKind::parameter, parameter.name, program.project_name,
                          parameter.location});
    }
    for (const auto& material : program.materials) {
        add_symbol(result, names,
                   Symbol{SymbolKind::material, material.name, program.project_name,
                          material.location});
    }
    for (const auto& profile : program.profiles) {
        add_symbol(result, names,
                   Symbol{SymbolKind::profile, profile.name, program.project_name,
                          profile.location});
    }
    for (const auto& sketch : program.sketches) {
        add_symbol(result, names,
                   Symbol{SymbolKind::sketch, sketch.name, program.project_name, sketch.location});
        const std::string sketch_scope = program.project_name + "::" + sketch.name;
        for (const auto& point : sketch.points) {
            add_symbol(result, names,
                       Symbol{SymbolKind::sketch_point, point.name, sketch_scope, point.location});
        }
        for (const auto& constraint : sketch.constraints) {
            add_symbol(result, names,
                       Symbol{SymbolKind::sketch_constraint, constraint.name, sketch_scope,
                              constraint.location});
        }
    }
    for (const auto& body : program.bodies) {
        add_symbol(result, names,
                   Symbol{SymbolKind::body, body.name, program.project_name, body.location});
        const std::string body_scope = program.project_name + "::" + body.name;
        for (const auto& feature : body.features) {
            add_symbol(result, names,
                       Symbol{SymbolKind::feature, feature.name, body_scope, feature.location});
        }
    }
    for (const auto& instance : program.instances) {
        add_symbol(result, names,
                   Symbol{SymbolKind::instance, instance.name, program.project_name,
                          instance.location});
    }
    for (const auto& scene : program.scenes) {
        add_symbol(result, names,
                   Symbol{SymbolKind::scene, scene.name, program.project_name, scene.location});
        const std::string scene_scope = program.project_name + "::" + scene.name;
        for (const auto& track : scene.tracks) {
            add_symbol(result, names,
                       Symbol{SymbolKind::animation_track, track.name, scene_scope,
                              track.location});
        }
    }
    for (const auto& constraint : program.constraints) {
        add_symbol(result, names,
                   Symbol{SymbolKind::constraint, constraint.name, program.project_name,
                          constraint.location});
    }
    return result;
}

} // namespace icad::compiler
