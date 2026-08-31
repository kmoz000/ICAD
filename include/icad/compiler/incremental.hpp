#pragma once

#include "icad/compiler/compiler.hpp"
#include "icad/compiler/dependency_graph.hpp"
#include "icad/cad/model.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace icad::compiler {

struct IncrementalBodyTiming {
    std::string body;
    double milliseconds{};
    std::size_t triangles{};
};

struct IncrementalReport {
    std::vector<std::string> reused_bodies;
    std::vector<std::string> recomputed_bodies;
    std::vector<std::string> removed_bodies;
    std::size_t worker_count{};
    double frontend_ms{};
    double fingerprint_ms{};
    double geometry_ms{};
    double merge_ms{};
    std::vector<IncrementalBodyTiming> body_timings;
};

struct IncrementalCompileResult {
    CompileResult compilation;
    DependencyGraph dependencies;
    IncrementalReport incremental;
    std::optional<cad::Model> model;
};

class IncrementalCompiler {
  public:
    [[nodiscard]] auto compile(std::string_view source, CompileOptions options = {})
        -> IncrementalCompileResult;
    auto clear() -> void;

  private:
    struct CachedBody {
        std::uint64_t fingerprint{};
        std::vector<cad::SolidTopology> solids;
        std::vector<cad::Part> definition_parts;
        std::vector<cad::Part> occurrence_parts;
    };
    std::unordered_map<std::string, CachedBody> bodies_;
    std::string project_name_;
    std::mutex mutex_;
};

} // namespace icad::compiler
