#include "icad/engine/c_api.h"

#include "icad/engine/session.hpp"
#include "icad/json/value.hpp"

#include <cstring>
#include <filesystem>
#include <new>
#include <string>

struct icad_engine_session {
    explicit icad_engine_session(const char* path) : session{std::filesystem::path{path}} {}
    icad::engine::Session session;
};

namespace {

[[nodiscard]] auto duplicate(const std::string& value) -> char* {
    auto* copy = new (std::nothrow) char[value.size() + 1];
    if (copy == nullptr)
        return nullptr;
    std::memcpy(copy, value.c_str(), value.size() + 1);
    return copy;
}

[[nodiscard]] auto error_json(std::string message) -> char* {
    return duplicate(icad::json::serialize(
        icad::json::Value::Object{{"success", false}, {"message", std::move(message)}}));
}

[[nodiscard]] auto diagnostics(const std::vector<icad::compiler::Diagnostic>& source)
    -> icad::json::Value::Array {
    icad::json::Value::Array result;
    result.reserve(source.size());
    for (const auto& diagnostic : source) {
        result.emplace_back(icad::json::Value::Object{
            {"code", diagnostic.code},
            {"message", diagnostic.message},
            {"line", static_cast<double>(diagnostic.location.line)},
            {"column", static_cast<double>(diagnostic.location.column)},
        });
    }
    return result;
}

} // namespace

extern "C" {

auto icad_engine_session_create(const char* source_path) -> icad_engine_session* {
    if (source_path == nullptr)
        return nullptr;
    return new (std::nothrow) icad_engine_session{source_path};
}

void icad_engine_session_destroy(icad_engine_session* session) { delete session; }

auto icad_engine_session_ready(const icad_engine_session* session) -> int {
    return session != nullptr && session->session.ready() ? 1 : 0;
}

auto icad_engine_session_error(const icad_engine_session* session) -> char* {
    return session == nullptr ? duplicate("invalid engine session")
                              : duplicate(session->session.error());
}

auto icad_engine_session_source(const icad_engine_session* session) -> char* {
    return session == nullptr ? nullptr : duplicate(session->session.source());
}

auto icad_engine_session_source_path(const icad_engine_session* session) -> char* {
    return session == nullptr ? nullptr : duplicate(session->session.source_path().string());
}

auto icad_engine_session_default_export_directory(const icad_engine_session* session) -> char* {
    return session == nullptr
               ? nullptr
               : duplicate(session->session.default_export_directory().string());
}

auto icad_engine_session_preview_json(icad_engine_session* session, const char* source,
                                      size_t source_size) -> char* {
    if (session == nullptr || (source == nullptr && source_size != 0))
        return error_json("invalid preview arguments");
    const auto result = session->session.preview(std::string_view{source, source_size});
    auto value = icad::json::Value::Object{
        {"success", result.success},
        {"message", result.message},
        {"revision", static_cast<double>(result.revision)},
        {"bodies", static_cast<double>(result.bodies)},
        {"materials", static_cast<double>(result.materials)},
        {"scenes", static_cast<double>(result.scenes)},
        {"keyframes", static_cast<double>(result.keyframes)},
        {"reusedBodies", static_cast<double>(result.reused_bodies)},
        {"recomputedBodies", static_cast<double>(result.recomputed_bodies)},
        {"parallelWorkers", static_cast<double>(result.parallel_workers)},
        {"milliseconds", result.milliseconds},
        {"unchanged", result.unchanged},
        {"diagnostics", diagnostics(result.diagnostics)},
    };
    auto serialized = icad::json::serialize(value);
    if (result.success && !result.model_json.empty()) {
        serialized.pop_back();
        serialized += ",\"model\":" + result.model_json + '}';
    }
    return duplicate(serialized);
}

auto icad_engine_session_save_json(icad_engine_session* session, const char* source,
                                   size_t source_size) -> char* {
    if (session == nullptr || (source == nullptr && source_size != 0))
        return error_json("invalid save arguments");
    const auto result = session->session.save(std::string_view{source, source_size});
    return duplicate(icad::json::serialize(
        icad::json::Value::Object{{"success", result.success}, {"message", result.message}}));
}

auto icad_engine_session_export_json(icad_engine_session* session, const char* source,
                                     size_t source_size, const char* output_directory) -> char* {
    if (session == nullptr || output_directory == nullptr ||
        (source == nullptr && source_size != 0))
        return error_json("invalid export arguments");
    const auto result = session->session.export_package(
        std::string_view{source, source_size}, std::filesystem::path{output_directory});
    return duplicate(icad::json::serialize(icad::json::Value::Object{
        {"success", result.success},
        {"message", result.message},
        {"directory", result.directory.string()},
        {"artifacts", static_cast<double>(result.artifacts)},
        {"components", static_cast<double>(result.components)},
        {"solids", static_cast<double>(result.solids)},
        {"milliseconds", result.milliseconds},
        {"diagnostics", diagnostics(result.diagnostics)},
    }));
}

void icad_engine_string_free(char* value) { delete[] value; }

} // extern "C"
