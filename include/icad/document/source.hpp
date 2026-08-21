#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace icad::document {

enum class SourceStatus {
    ok,
    path_error,
    not_found,
    conflict,
    invalid_source,
    io_error,
};

struct SourceSnapshot {
    SourceStatus status{SourceStatus::io_error};
    std::string message;
    std::filesystem::path path;
    std::string source;
    std::string revision;

    [[nodiscard]] auto ok() const -> bool { return status == SourceStatus::ok; }
};

struct SourceChange {
    SourceStatus status{SourceStatus::io_error};
    std::string message;
    std::filesystem::path path;
    std::string previous_revision;
    std::string revision;
    std::vector<compiler::Diagnostic> diagnostics;

    [[nodiscard]] auto ok() const -> bool { return status == SourceStatus::ok; }
};

struct SourceHistory {
    SourceStatus status{SourceStatus::io_error};
    std::string message;
    std::vector<std::string> revisions;

    [[nodiscard]] auto ok() const -> bool { return status == SourceStatus::ok; }
};

struct ParameterEdit {
    std::string name;
    double value{};
    std::string unit;
};

inline constexpr std::string_view absent_revision = "absent";

[[nodiscard]] auto read_source(const std::filesystem::path& workspace,
                               const std::filesystem::path& relative_path) -> SourceSnapshot;
[[nodiscard]] auto write_source(const std::filesystem::path& workspace,
                                const std::filesystem::path& relative_path,
                                std::string source,
                                std::string_view expected_revision) -> SourceChange;
[[nodiscard]] auto set_parameter(const std::filesystem::path& workspace,
                                 const std::filesystem::path& relative_path,
                                 std::string_view parameter,
                                 double value,
                                 std::string_view unit,
                                 std::string_view expected_revision) -> SourceChange;
[[nodiscard]] auto set_parameters(const std::filesystem::path& workspace,
                                  const std::filesystem::path& relative_path,
                                  const std::vector<ParameterEdit>& edits,
                                  std::string_view expected_revision) -> SourceChange;
[[nodiscard]] auto source_history(const std::filesystem::path& workspace,
                                  const std::filesystem::path& relative_path) -> SourceHistory;
[[nodiscard]] auto restore_source(const std::filesystem::path& workspace,
                                  const std::filesystem::path& relative_path,
                                  std::string_view target_revision,
                                  std::string_view expected_revision) -> SourceChange;

} // namespace icad::document
