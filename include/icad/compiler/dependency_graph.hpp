#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace icad::compiler {

struct DependencyNode {
    std::string id;
    std::string kind;
    std::vector<std::string> dependencies;
};

struct DependencyEdge {
    std::size_t dependency{};
    std::size_t consumer{};
};

struct DependencyGraph {
    std::vector<DependencyNode> nodes;
    // Compact integer edges are ready for graph layout/rendering without
    // resolving string IDs every frame. Node IDs remain the stable API.
    std::vector<DependencyEdge> edges;
    std::vector<std::string> evaluation_order;
    std::size_t edge_count{};
};

[[nodiscard]] auto build_dependency_graph(const ir::Project& project) -> DependencyGraph;

} // namespace icad::compiler
