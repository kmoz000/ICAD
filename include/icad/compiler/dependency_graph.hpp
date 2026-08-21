#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <vector>

namespace icad::compiler {

struct DependencyNode {
    std::string id;
    std::string kind;
    std::vector<std::string> dependencies;
};

struct DependencyGraph {
    std::vector<DependencyNode> nodes;
    std::vector<std::string> evaluation_order;
    std::size_t edge_count{};
};

[[nodiscard]] auto build_dependency_graph(const ir::Project& project) -> DependencyGraph;

} // namespace icad::compiler
