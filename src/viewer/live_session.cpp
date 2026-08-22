#include "icad/viewer/live_session.hpp"

#include "icad/compiler/compiler.hpp"
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
    if (source == last_preview_source_ && last_preview_.success &&
        unchanged_imports(import_stamps_)) {
        result = last_preview_;
        result.unchanged = true;
        result.reused_bodies = result.bodies;
        result.recomputed_bodies = 0;
        result.milliseconds = 0.0;
        result.message = "unchanged live preview reused";
        return result;
    }
    auto incremental = compiler_.compile(
        source, compiler::CompileOptions{.build_topology = false,
                                         .imports = {.source_path = source_path_,
                                                     .project_root = source_path_.parent_path()}});
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
    result.model_json = scene::web_model_json(*compilation.ir_project, *incremental.model);
    if (result.model_json.empty()) {
        result.message = "live preview model serialization failed";
        return result;
    }

    ++revision_;
    result.success = true;
    result.message = "live preview compiled";
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
    // The webview already owns the mounted model. Retain only compact metadata
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
