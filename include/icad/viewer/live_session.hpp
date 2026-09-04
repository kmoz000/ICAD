#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/incremental.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace icad::viewer {

struct PreviewResult {
    bool success{false};
    bool engineering_valid{true};
    std::string message;
    std::string model_json;
    std::string evidence_json;
    std::uint64_t revision{};
    std::size_t bodies{};
    std::size_t materials{};
    std::size_t scenes{};
    std::size_t keyframes{};
    std::size_t reused_bodies{};
    std::size_t recomputed_bodies{};
    std::size_t parallel_workers{};
    double milliseconds{};
    double compile_ms{};
    double frontend_ms{};
    double fingerprint_ms{};
    double geometry_ms{};
    double merge_ms{};
    double analysis_ms{};
    double validation_ms{};
    double serialization_ms{};
    std::vector<compiler::IncrementalBodyTiming> body_timings;
    bool unchanged{};
    std::vector<compiler::Diagnostic> diagnostics;
};

struct SaveResult {
    bool success{false};
    std::string message;
};

struct PackageResult {
    bool success{false};
    std::string message;
    std::filesystem::path directory;
    std::size_t artifacts{};
    std::size_t components{};
    std::size_t solids{};
    double milliseconds{};
    std::vector<compiler::Diagnostic> diagnostics;
};

class LiveSession {
  public:
    explicit LiveSession(std::filesystem::path source_path);
    LiveSession(const LiveSession&) = delete;
    auto operator=(const LiveSession&) -> LiveSession& = delete;
    ~LiveSession() = default;

    [[nodiscard]] auto ready() const noexcept -> bool { return error_.empty(); }
    [[nodiscard]] auto error() const -> const std::string& { return error_; }
    [[nodiscard]] auto source() const -> const std::string& { return source_; }
    [[nodiscard]] auto source_path() const -> const std::filesystem::path& {
        return source_path_;
    }
    [[nodiscard]] auto preview(std::string_view source) -> PreviewResult;
    [[nodiscard]] auto save(std::string_view source) -> SaveResult;
    [[nodiscard]] auto export_package(std::string_view source,
                                      const std::filesystem::path& directory) -> PackageResult;
    [[nodiscard]] auto default_export_directory() const -> std::filesystem::path;

  private:
    std::filesystem::path source_path_;
    std::string source_;
    std::string error_;
    std::uint64_t revision_{};
    compiler::IncrementalCompiler compiler_;
    std::string last_preview_source_;
    PreviewResult last_preview_;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> import_stamps_;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> evidence_stamps_;
    bool evidence_exists_{};
    std::filesystem::file_time_type evidence_stamp_{};
};

} // namespace icad::viewer
