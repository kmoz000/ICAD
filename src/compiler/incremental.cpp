#include "icad/compiler/incremental.hpp"

#include "icad/cad/topology.hpp"
#include "icad/cad/model.hpp"
#include "icad/document/revision.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

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
    // Joint-driven occurrence placement can depend on spatial declarations far
    // outside a body's feature block. Including these compact semantic records
    // keeps preview reuse conservative and exact when a mechanism is edited.
    dependency.points = project.points;
    dependency.vectors = project.vectors;
    dependency.joints = project.joints;
    return document::fingerprint(dependency);
}

[[nodiscard]] auto body_project(const ir::Project& project, const ir::Body& body) -> ir::Project {
    // Geometry workers receive only the canonical fields consumed by the
    // native model/topology builders. Avoid cloning scenes, textures, sketches,
    // reports, and every other body once per worker on very large projects.
    ir::Project single;
    single.name = project.name;
    single.canonical_length_unit = project.canonical_length_unit;
    single.tolerance = project.tolerance;
    single.points = project.points;
    single.vectors = project.vectors;
    single.joints = project.joints;
    single.profiles = project.profiles;
    single.bodies.push_back(body);
    std::ranges::copy_if(project.poses, std::back_inserter(single.poses),
                         [&](const auto& pose) { return pose.body == body.name; });
    std::ranges::copy_if(project.instances, std::back_inserter(single.instances),
                         [&](const auto& instance) { return instance.body == body.name; });
    return single;
}

} // namespace

auto IncrementalCompiler::compile(std::string_view source, CompileOptions options)
    -> IncrementalCompileResult {
    // The cache is deliberately protected as one coherent revision. Geometry
    // for dirty bodies is parallel below, while callers may safely share one
    // compiler without observing or publishing a partially merged revision.
    const std::lock_guard cache_lock{mutex_};
    IncrementalCompileResult result;
    options.build_topology = false;
    result.compilation = compiler::compile(source, std::move(options));
    if (!result.compilation.ok())
        return result;
    const auto& project = *result.compilation.ir_project;
    result.dependencies = build_dependency_graph(project);
    if (project.name != project_name_) {
        bodies_.clear();
        project_name_.clear();
    }

    struct BodySelection {
        const ir::Body* body{};
        std::uint64_t fingerprint{};
        bool reused{};
    };
    struct ComputedBody {
        cad::TopologyModel topology;
        CachedBody cached;
        std::vector<Diagnostic> diagnostics;
    };

    std::vector<BodySelection> selections;
    selections.reserve(project.bodies.size());
    std::vector<std::size_t> dirty;
    dirty.reserve(project.bodies.size());
    for (const auto& body : project.bodies) {
        const auto fingerprint = body_fingerprint(project, body);
        const auto found = bodies_.find(body.name);
        const bool reused = found != bodies_.end() && found->second.fingerprint == fingerprint;
        selections.push_back({&body, fingerprint, reused});
        if (!reused)
            dirty.push_back(selections.size() - 1U);
    }

    std::vector<std::optional<ComputedBody>> computed(project.bodies.size());
    if (!dirty.empty()) {
        constexpr std::size_t max_workers = 8;
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        const auto worker_count =
            std::min({dirty.size(), static_cast<std::size_t>(hardware), max_workers});
        result.incremental.worker_count = worker_count;
        std::atomic_size_t next_job{};
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&] {
                while (true) {
                    const auto job = next_job.fetch_add(1, std::memory_order_relaxed);
                    if (job >= dirty.size())
                        return;
                    const auto selection_index = dirty[job];
                    const auto& selection = selections[selection_index];
                    const auto single_body = body_project(project, *selection.body);
                    auto topology = cad::build_topology(single_body);
                    auto model = cad::build_model(single_body);
                    ComputedBody next{std::move(topology),
                                      CachedBody{selection.fingerprint, {}, {}, {}}, {}};
                    next.cached.solids = next.topology.solids;
                    const auto validation = cad::validate_topology(next.topology);
                    for (const auto& issue : validation.issues) {
                        next.diagnostics.push_back(
                            {DiagnosticSeverity::error, issue.code,
                             issue.message + " [" + issue.entity + "]", {1, 1}});
                    }
                    if (!cad::is_valid(model)) {
                        next.diagnostics.push_back(
                            {DiagnosticSeverity::error, "ICAD-G0099",
                             "delivery model validation failed [" + selection.body->name + "]",
                             {1, 1}});
                    }
                    for (auto& part : model.parts) {
                        if (part.body == selection.body->name)
                            next.cached.definition_parts.push_back(std::move(part));
                        else
                            next.cached.occurrence_parts.push_back(std::move(part));
                    }
                    computed[selection_index] = std::move(next);
                }
            });
        }
    }

    std::unordered_set<std::string> current_names;
    cad::TopologyModel merged;
    cad::Model merged_model;
    std::vector<cad::Part> merged_occurrences;
    for (std::size_t index = 0; index < selections.size(); ++index) {
        const auto& selection = selections[index];
        const auto& body = *selection.body;
        current_names.insert(body.name);
        if (selection.reused) {
            const auto& cached = bodies_.at(body.name);
            result.incremental.reused_bodies.push_back(body.name);
            merged.solids.insert(merged.solids.end(), cached.solids.begin(), cached.solids.end());
            merged_model.parts.insert(merged_model.parts.end(),
                                      cached.definition_parts.begin(),
                                      cached.definition_parts.end());
            merged_occurrences.insert(merged_occurrences.end(),
                                      cached.occurrence_parts.begin(),
                                      cached.occurrence_parts.end());
            continue;
        }
        auto next = std::move(*computed[index]);
        result.compilation.diagnostics.insert(
            result.compilation.diagnostics.end(),
            std::make_move_iterator(next.diagnostics.begin()),
            std::make_move_iterator(next.diagnostics.end()));
        result.incremental.recomputed_bodies.push_back(body.name);
        merged_model.parts.insert(merged_model.parts.end(), next.cached.definition_parts.begin(),
                                  next.cached.definition_parts.end());
        merged_occurrences.insert(merged_occurrences.end(), next.cached.occurrence_parts.begin(),
                                  next.cached.occurrence_parts.end());
        merged.solids.insert(merged.solids.end(), next.topology.solids.begin(),
                             next.topology.solids.end());
        bodies_[body.name] = std::move(next.cached);
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
    if (result.compilation.diagnostics.empty()) {
        result.compilation.topology_model = std::move(merged);
        merged_model.parts.insert(merged_model.parts.end(),
                                  std::make_move_iterator(merged_occurrences.begin()),
                                  std::make_move_iterator(merged_occurrences.end()));
        result.model = std::move(merged_model);
    }
    return result;
}

auto IncrementalCompiler::clear() -> void {
    const std::lock_guard cache_lock{mutex_};
    bodies_.clear();
    project_name_.clear();
}

} // namespace icad::compiler
