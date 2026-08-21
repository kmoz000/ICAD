#include "icad/compiler/incremental.hpp"

#include "icad/cad/topology.hpp"
#include "icad/document/revision.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace icad::compiler {
namespace {

[[nodiscard]] auto body_fingerprint(const ir::Project& project, const ir::Body& body)
    -> std::uint64_t {
    ir::Project dependency;
    dependency.name = project.name + "::" + body.name;
    dependency.canonical_length_unit = project.canonical_length_unit;
    dependency.bodies.push_back(body);
    for (const auto& material : project.materials) {
        if (material.name == body.material)
            dependency.materials.push_back(material);
    }
    for (const auto& pose : project.poses) {
        if (pose.body == body.name)
            dependency.poses.push_back(pose);
    }
    for (const auto& instance : project.instances) {
        if (instance.body == body.name)
            dependency.instances.push_back(instance);
    }
    const auto references_profile = [&](const std::string& name) {
        return std::ranges::any_of(body.features, [&](const auto& feature) {
            return feature.profile == name || feature.target_profile == name;
        });
    };
    for (const auto& profile : project.profiles) {
        if (references_profile(profile.name))
            dependency.profiles.push_back(profile);
    }
    const auto references_point = [&](const std::string& name) {
        return std::ranges::any_of(body.features, [&](const auto& feature) {
            return feature.selected_edge_point == name || feature.plane_point == name ||
                   std::ranges::contains(feature.path_points, name);
        });
    };
    for (const auto& point : project.points) {
        if (references_point(point.name))
            dependency.points.push_back(point);
    }
    const auto references_vector = [&](const std::string& name) {
        return std::ranges::any_of(body.features, [&](const auto& feature) {
            return feature.direction == name || feature.plane_normal == name;
        });
    };
    for (const auto& direction : project.vectors) {
        if (references_vector(direction.name))
            dependency.vectors.push_back(direction);
    }
    return document::fingerprint(dependency);
}

[[nodiscard]] auto body_project(const ir::Project& project, const ir::Body& body) -> ir::Project {
    ir::Project single = project;
    single.bodies.clear();
    single.bodies.push_back(body);
    std::erase_if(single.instances,
                  [&](const auto& instance) { return instance.body != body.name; });
    return single;
}

} // namespace

auto IncrementalCompiler::compile(std::string_view source) -> IncrementalCompileResult {
    IncrementalCompileResult result;
    result.compilation = compiler::compile(source, CompileOptions{.build_topology = false});
    if (!result.compilation.ok())
        return result;
    const auto& project = *result.compilation.ir_project;
    result.dependencies = build_dependency_graph(project);
    if (project.name != project_name_)
        clear();

    std::unordered_set<std::string> current_names;
    cad::TopologyModel merged;
    for (const auto& body : project.bodies) {
        current_names.insert(body.name);
        const std::uint64_t fingerprint = body_fingerprint(project, body);
        const auto cached = bodies_.find(body.name);
        if (cached != bodies_.end() && cached->second.fingerprint == fingerprint) {
            result.incremental.reused_bodies.push_back(body.name);
            merged.solids.insert(merged.solids.end(), cached->second.solids.begin(),
                                 cached->second.solids.end());
            continue;
        }
        const auto topology = cad::build_topology(body_project(project, body));
        const auto validation = cad::validate_topology(topology);
        for (const auto& issue : validation.issues) {
            result.compilation.diagnostics.push_back(
                {DiagnosticSeverity::error, issue.code,
                 issue.message + " [" + issue.entity + "]", {1, 1}});
        }
        result.incremental.recomputed_bodies.push_back(body.name);
        bodies_[body.name] = CachedBody{fingerprint, topology.solids};
        merged.solids.insert(merged.solids.end(), topology.solids.begin(), topology.solids.end());
    }
    for (const auto& [name, cached] : bodies_) {
        static_cast<void>(cached);
        if (!current_names.contains(name))
            result.incremental.removed_bodies.push_back(name);
    }
    std::ranges::sort(result.incremental.removed_bodies);
    for (const auto& name : result.incremental.removed_bodies)
        bodies_.erase(name);
    project_name_ = project.name;
    if (result.compilation.diagnostics.empty())
        result.compilation.topology_model = std::move(merged);
    return result;
}

auto IncrementalCompiler::clear() -> void {
    bodies_.clear();
    project_name_.clear();
}

} // namespace icad::compiler
