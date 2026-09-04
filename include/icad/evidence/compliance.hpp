#pragma once

#include "icad/compiler/ir/ir.hpp"
#include "icad/json/value.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace icad::evidence {

enum class Severity { information, warning, error };

struct Issue {
    std::string code;
    Severity severity{Severity::error};
    std::string path;
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

struct Evaluation {
    bool manifest_valid{};
    bool release_ready{};
    std::string lifecycle_state;
    std::string basis;
    std::string model_revision;
    std::string model_sha256;
    std::vector<Issue> issues;
    json::Value normalized_manifest;
    json::Value compliance;
};

[[nodiscard]] auto sha256(std::string_view bytes) -> std::string;
[[nodiscard]] auto sha256_file(const std::filesystem::path& path) -> std::string;

[[nodiscard]] auto evaluate(std::string_view model_source,
                            const compiler::ir::Project& project,
                            std::string_view manifest_source,
                            const std::filesystem::path& manifest_path,
                            std::string_view requested_basis = {}) -> Evaluation;

[[nodiscard]] auto evidence_json(const Evaluation& evaluation) -> std::string;
[[nodiscard]] auto compliance_json(const Evaluation& evaluation) -> std::string;
[[nodiscard]] auto compliance_html(const Evaluation& evaluation) -> std::string;

} // namespace icad::evidence
