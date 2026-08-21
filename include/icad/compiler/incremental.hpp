#pragma once

#include "icad/compiler/compiler.hpp"
#include "icad/compiler/dependency_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace icad::compiler {

struct IncrementalReport {
    std::vector<std::string> reused_bodies;
    std::vector<std::string> recomputed_bodies;
    std::vector<std::string> removed_bodies;
};

struct IncrementalCompileResult {
    CompileResult compilation;
    DependencyGraph dependencies;
    IncrementalReport incremental;
};

class IncrementalCompiler {
  public:
    [[nodiscard]] auto compile(std::string_view source) -> IncrementalCompileResult;
    auto clear() -> void;

  private:
    struct CachedBody {
        std::uint64_t fingerprint{};
        std::vector<cad::SolidTopology> solids;
    };
    std::unordered_map<std::string, CachedBody> bodies_;
    std::string project_name_;
};

} // namespace icad::compiler
