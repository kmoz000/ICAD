#include "icad/viewer/live_session.hpp"

#include "icad/cad/analysis.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/evidence/compliance.hpp"
#include "icad/manufacturing/validator.hpp"
#include "icad/project/builder.hpp"
#include "icad/scene/exporter.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace icad::viewer {
namespace {

[[nodiscard]] auto unique_token() -> std::uint64_t {
    static std::atomic_uint64_t sequence{};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return now ^ sequence.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] auto unchanged_imports(
    const std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>& stamps)
    -> bool {
    return std::ranges::all_of(stamps, [](const auto& stamp) {
        std::error_code error;
        const auto current = std::filesystem::last_write_time(stamp.first, error);
        return !error && current == stamp.second;
    });
}

[[nodiscard]] auto evidence_path(const std::filesystem::path& source_path)
    -> std::filesystem::path {
    auto path = source_path;
    path.replace_extension(".evidence.json");
    return path;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream input{path, std::ios::binary};
    if (!input)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto evidence_dependency_digests(const std::filesystem::path& manifest_path,
                                               std::string_view manifest_source)
    -> std::vector<std::pair<std::filesystem::path, std::string>> {
    std::vector<std::pair<std::filesystem::path, std::string>> digests;
    const auto append = [&](const std::filesystem::path& path) {
        const auto content = read_file(path);
        if (content)
            digests.emplace_back(path, evidence::sha256(*content));
    };
    append(manifest_path);
    const auto parsed = json::parse(manifest_source);
    if (!parsed.ok())
        return digests;
    for (const auto field : {"controlledInputs", "artifacts"}) {
        const auto* value = parsed.value->find(field);
        const auto* entries = value == nullptr ? nullptr : value->array();
        if (entries == nullptr)
            continue;
        for (const auto& entry : *entries) {
            const auto* path_value = entry.find("path");
            const auto* relative = path_value == nullptr ? nullptr : path_value->string();
            if (relative == nullptr)
                continue;
            const std::filesystem::path dependency{*relative};
            append(dependency.is_absolute()
                       ? dependency.lexically_normal()
                       : (manifest_path.parent_path() / dependency).lexically_normal());
        }
    }
    return digests;
}

[[nodiscard]] auto declaration_location(std::string_view source, std::string_view name)
    -> compiler::SourceLocation {
    constexpr std::string_view prefixes[]{"MATE ", "CONSTRAINT ", "INTERFACE ", "CONNECT ",
                                          "BODY ", "INSTANCE "};
    std::size_t position = std::string_view::npos;
    for (const auto prefix : prefixes) {
        position = source.find(std::string{prefix} + std::string{name});
        if (position != std::string_view::npos)
            break;
    }
    if (position == std::string_view::npos)
        return {1, 1};
    const auto line_start = source.rfind('\n', position);
    return {1 + static_cast<std::size_t>(
                    std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(position),
                               '\n')),
            position - (line_start == std::string_view::npos ? 0 : line_start + 1) + 1};
}

} // namespace

LiveSession::LiveSession(std::filesystem::path source_path)
    : source_path_{std::filesystem::absolute(std::move(source_path)).lexically_normal()} {
    if (source_path_.extension() != ".icad" || !std::filesystem::is_regular_file(source_path_)) {
        error_ = "expected an existing .icad source file";
        return;
    }
    std::ifstream input{source_path_, std::ios::binary};
    if (!input) {
        error_ = "cannot read ICAD source";
        return;
    }
    source_ = {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

auto LiveSession::preview(std::string_view source) -> PreviewResult {
    const auto started = std::chrono::steady_clock::now();
    PreviewResult result;
    if (!ready()) {
        result.message = error_;
        return result;
    }
    const auto manifest_path = evidence_path(source_path_);
    std::error_code evidence_error;
    const bool evidence_exists = std::filesystem::is_regular_file(manifest_path, evidence_error);
    const auto manifest = evidence_exists ? read_file(manifest_path) : std::nullopt;
    const auto evidence_digests = manifest ? evidence_dependency_digests(manifest_path, *manifest)
                                           : decltype(evidence_digests_){};
    const bool evidence_unchanged = evidence_exists == evidence_exists_ &&
                                    (!evidence_exists ||
                                     (!evidence_error && evidence_digests == evidence_digests_));
    if (source == last_preview_source_ && last_preview_.success &&
        unchanged_imports(import_stamps_) && evidence_unchanged) {
        result = last_preview_;
        result.unchanged = true;
        result.reused_bodies = result.bodies;
        result.recomputed_bodies = 0;
        result.milliseconds = 0.0;
        result.compile_ms = 0.0;
        result.frontend_ms = 0.0;
        result.fingerprint_ms = 0.0;
        result.geometry_ms = 0.0;
        result.merge_ms = 0.0;
        result.analysis_ms = 0.0;
        result.validation_ms = 0.0;
        result.serialization_ms = 0.0;
        result.message = "unchanged live preview reused";
        return result;
    }
    auto incremental = compiler_.compile(
        source, compiler::CompileOptions{.build_topology = false,
                                         .imports = {.source_path = source_path_,
                                                     .project_root = source_path_.parent_path()}});
    const auto compiled_at = std::chrono::steady_clock::now();
    result.compile_ms =
        std::chrono::duration<double, std::milli>(compiled_at - started).count();
    result.frontend_ms = incremental.incremental.frontend_ms;
    result.fingerprint_ms = incremental.incremental.fingerprint_ms;
    result.geometry_ms = incremental.incremental.geometry_ms;
    result.merge_ms = incremental.incremental.merge_ms;
    result.body_timings = incremental.incremental.body_timings;
    auto& compilation = incremental.compilation;
    if (!compilation.ok()) {
        result.message = std::to_string(compilation.diagnostics.size()) +
                         (compilation.diagnostics.size() == 1 ? " compiler error" :
                                                               " compiler errors");
        result.diagnostics = std::move(compilation.diagnostics);
        result.milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
        return result;
    }

    if (!incremental.model) {
        result.message = "incremental preview did not produce a delivery model";
        return result;
    }
    const auto& project = *compilation.ir_project;
    if (manifest) {
        const auto evaluation = evidence::evaluate(source, project, *manifest, manifest_path);
        result.evidence_json = evidence::compliance_json(evaluation);
    }
    const auto analysis = cad::analyze(project, *incremental.model);
    const auto analyzed_at = std::chrono::steady_clock::now();
    result.analysis_ms =
        std::chrono::duration<double, std::milli>(analyzed_at - compiled_at).count();
    const auto constraint_results = constraints::validate(project, analysis);
    // Interactive preview must deliver geometry promptly. Full all-pairs solid
    // interference remains an explicit validate/export gate; running it before
    // every frame made large assemblies appear frozen. The fast pass still
    // checks dimensions, materials, constraints, and interface attachment.
    const auto manufacturing =
        manufacturing::validate(project, analysis, cad::IntersectionAnalysis{});
    const auto validated_at = std::chrono::steady_clock::now();
    result.validation_ms =
        std::chrono::duration<double, std::milli>(validated_at - analyzed_at).count();
    for (const auto& constraint : constraint_results) {
        if (constraint.passed)
            continue;
        result.diagnostics.push_back(
            {compiler::DiagnosticSeverity::error, "ICAD-V0001",
             constraint.message + ": required " + std::to_string(constraint.required_mm) +
                 " " + constraint.unit + ", actual " + std::to_string(constraint.actual_mm) +
                 " " + constraint.unit,
             declaration_location(source, constraint.name)});
    }
    for (const auto& issue : manufacturing.issues) {
        result.diagnostics.push_back(
            {issue.severity == manufacturing::Severity::error
                 ? compiler::DiagnosticSeverity::error
                 : issue.severity == manufacturing::Severity::warning
                       ? compiler::DiagnosticSeverity::warning
                       : compiler::DiagnosticSeverity::note,
             issue.code, issue.message, declaration_location(source, issue.subject)});
    }
    result.engineering_valid = constraints::all_passed(constraint_results) && manufacturing.passed;
    result.model_json = scene::render_model_json(*compilation.ir_project, *incremental.model);
    const auto serialized_at = std::chrono::steady_clock::now();
    result.serialization_ms =
        std::chrono::duration<double, std::milli>(serialized_at - validated_at).count();
    if (result.model_json.empty()) {
        result.message = "live preview model serialization failed";
        return result;
    }

    ++revision_;
    result.success = true;
    result.message = result.engineering_valid ? "live preview compiled"
                                              : "live preview compiled with engineering issues";
    result.revision = revision_;
    result.bodies = compilation.ir_project->bodies.size() +
                    compilation.ir_project->instances.size();
    result.materials = compilation.ir_project->materials.size();
    result.scenes = compilation.ir_project->scenes.size();
    for (const auto& scene : compilation.ir_project->scenes) {
        for (const auto& track : scene.tracks)
            result.keyframes += track.keyframes.size();
    }
    result.reused_bodies = incremental.incremental.reused_bodies.size();
    result.recomputed_bodies = incremental.incremental.recomputed_bodies.size();
    result.parallel_workers = incremental.incremental.worker_count;
    result.milliseconds = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    last_preview_source_.assign(source);
    import_stamps_.clear();
    import_stamps_.reserve(compilation.imported_files.size());
    for (const auto& dependency : compilation.imported_files) {
        std::error_code error;
        const auto timestamp = std::filesystem::last_write_time(dependency, error);
        if (!error)
            import_stamps_.emplace_back(dependency, timestamp);
    }
    last_preview_ = result;
    evidence_exists_ = evidence_exists;
    evidence_digests_ = evidence_digests;
    // The native viewport already owns the uploaded model. Retain only compact metadata
    // for unchanged-source responses instead of duplicating a potentially huge
    // mesh payload in the session cache.
    last_preview_.model_json.clear();
    return result;
}

auto LiveSession::default_export_directory() const -> std::filesystem::path {
    return source_path_.parent_path() / "build" / source_path_.stem();
}

auto LiveSession::export_package(std::string_view source,
                                 const std::filesystem::path& directory) -> PackageResult {
    const auto started = std::chrono::steady_clock::now();
    PackageResult result;
    if (!ready()) {
        result.message = error_;
        return result;
    }
    if (directory.empty()) {
        result.message = "export directory is empty";
        return result;
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(directory, error).lexically_normal();
    if (error) {
        result.message = "cannot resolve export directory: " + error.message();
        return result;
    }
    if (std::filesystem::exists(absolute, error) &&
        !std::filesystem::is_directory(absolute, error)) {
        result.message = "export destination is not a directory";
        return result;
    }
    auto compilation = compiler::compile(
        source, compiler::CompileOptions{.build_topology = true,
                                         .imports = {.source_path = source_path_,
                                                     .project_root = source_path_.parent_path()}});
    if (!compilation.ok()) {
        result.message = std::to_string(compilation.diagnostics.size()) +
                         " compiler errors prevent export";
        result.diagnostics = std::move(compilation.diagnostics);
        return result;
    }
    const auto built =
        project::build(*compilation.ir_project, absolute, source_path_.stem().string());
    result.success = built.success;
    result.message = built.message;
    result.directory = std::move(absolute);
    result.artifacts = built.artifacts.size();
    result.components = built.components;
    result.solids = built.solids;
    result.milliseconds = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    return result;
}

auto LiveSession::save(std::string_view source) -> SaveResult {
    if (!ready())
        return {false, error_};
    const auto temporary = source_path_.parent_path() /
                           (source_path_.filename().string() + ".save-" +
                            std::to_string(unique_token()) + ".tmp");
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        output.flush();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return {false, "cannot write temporary source file"};
        }
    }

#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), source_path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        const auto message = std::system_category().message(static_cast<int>(GetLastError()));
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {false, "cannot replace source file: " + message};
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, source_path_, error);
    if (error) {
        const auto message = error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {false, "cannot replace source file: " + message};
    }
#endif
    source_.assign(source);
    return {true, "source saved"};
}

} // namespace icad::viewer
