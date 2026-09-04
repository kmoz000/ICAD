#pragma once

#include "icad/engine/c_api.h"
#include "icad/compiler/diagnostics/diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace icad::engine {

struct PreviewResult {
    bool success{};
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
    bool unchanged{};
    std::vector<compiler::Diagnostic> diagnostics;
};

struct SaveResult {
    bool success{};
    std::string message;
};

struct PackageResult {
    bool success{};
    std::string message;
    std::filesystem::path directory;
    std::size_t artifacts{};
    std::size_t components{};
    std::size_t solids{};
    double milliseconds{};
    std::vector<compiler::Diagnostic> diagnostics;
};

// Thread-safe native session shared by desktop hosts, editor integrations, and
// embedders. The implementation remains private so the library ABI does not
// expose compiler or viewer object layouts.
class ICAD_ENGINE_API Session {
  public:
    explicit Session(std::filesystem::path source_path);
    Session(const Session&) = delete;
    auto operator=(const Session&) -> Session& = delete;
    Session(Session&&) noexcept;
    auto operator=(Session&&) noexcept -> Session&;
    ~Session();

    [[nodiscard]] auto ready() const -> bool;
    [[nodiscard]] auto error() const -> std::string;
    [[nodiscard]] auto source() const -> std::string;
    [[nodiscard]] auto source_path() const -> std::filesystem::path;
    [[nodiscard]] auto default_export_directory() const -> std::filesystem::path;
    [[nodiscard]] auto preview(std::string_view source) -> PreviewResult;
    [[nodiscard]] auto save(std::string_view source) -> SaveResult;
    [[nodiscard]] auto export_package(std::string_view source,
                                      const std::filesystem::path& directory) -> PackageResult;

  private:
    class Impl;
    Impl* impl_{};
};

} // namespace icad::engine
