#include "icad/ai/inspector.hpp"
#include "icad/compiler/incremental.hpp"

#include <iostream>
#include <string>

namespace {

constexpr std::string_view initial_source = R"(PROJECT incremental
UNITS mm
PARAMETER driven_width 10 mm
BODY driven
FEATURE block
TYPE BOX
WIDTH driven_width
DEPTH 10 mm
HEIGHT 10 mm
END
END
BODY stable
FEATURE block
TYPE BOX
WIDTH 5 mm
DEPTH 5 mm
HEIGHT 5 mm
ORIGIN_X 20 mm
END
END
)";

} // namespace

int main() {
    icad::compiler::IncrementalCompiler compiler;
    const auto first = compiler.compile(initial_source);
    if (!first.compilation.ok() || first.incremental.recomputed_bodies.size() != 2 ||
        !first.incremental.reused_bodies.empty() ||
        first.dependencies.evaluation_order.size() != first.dependencies.nodes.size()) {
        std::cerr << "initial incremental compile or dependency DAG failed\n";
        return 1;
    }
    const auto unchanged = compiler.compile(initial_source);
    if (!unchanged.compilation.ok() || unchanged.incremental.reused_bodies.size() != 2 ||
        !unchanged.incremental.recomputed_bodies.empty()) {
        std::cerr << "unchanged body topology was not reused\n";
        return 1;
    }
    std::string changed{initial_source};
    changed.replace(changed.find("PARAMETER driven_width 10 mm"),
                    std::string_view{"PARAMETER driven_width 10 mm"}.size(),
                    "PARAMETER driven_width 12 mm");
    const auto updated = compiler.compile(changed);
    if (!updated.compilation.ok() || updated.incremental.recomputed_bodies !=
                                             std::vector<std::string>{"driven"} ||
        updated.incremental.reused_bodies != std::vector<std::string>{"stable"} ||
        !updated.compilation.topology_model ||
        updated.compilation.topology_model->solids.size() != 2) {
        std::cerr << "dirty-body dependency closure was not recomputed selectively\n";
        return 1;
    }
    const auto inspection = icad::ai::project_json(*updated.compilation.ir_project);
    if (!inspection.contains("\"id\":\"parameter:driven_width\"") ||
        !inspection.contains("\"id\":\"feature:driven/block\"") ||
        !inspection.contains("\"dependsOn\":[\"feature:driven/block\"]")) {
        std::cerr << "agent dependency graph inspection is incomplete\n";
        return 1;
    }
    compiler.clear();
    const auto cleared = compiler.compile(changed);
    if (cleared.incremental.recomputed_bodies.size() != 2)
        return 1;
    return 0;
}
