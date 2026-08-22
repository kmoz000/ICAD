#include "icad/ai/inspector.hpp"
#include "icad/compiler/incremental.hpp"

#include <iostream>
#include <array>
#include <algorithm>
#include <thread>
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
        !first.model || first.model->parts.size() != 2 ||
        first.dependencies.evaluation_order.size() != first.dependencies.nodes.size()) {
        std::cerr << "initial incremental compile or dependency DAG failed\n";
        return 1;
    }
    const auto unchanged = compiler.compile(initial_source);
    if (!unchanged.compilation.ok() || unchanged.incremental.reused_bodies.size() != 2 ||
        !unchanged.incremental.recomputed_bodies.empty() || !unchanged.model ||
        unchanged.model->parts.size() != 2) {
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
        !updated.compilation.topology_model || !updated.model ||
        updated.model->parts.size() != 2 ||
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

    // One compiler may be shared by editor/LSP/agent callers. Each call sees a
    // complete cache revision even while dirty-body geometry itself is built in
    // parallel.
    std::array<bool, 4> concurrent_ok{};
    std::array<std::jthread, 4> callers;
    for (std::size_t index = 0; index < callers.size(); ++index) {
        callers[index] = std::jthread([&compiler, &concurrent_ok, index, &changed] {
            const auto result = compiler.compile(changed);
            concurrent_ok[index] = result.compilation.ok() && result.model.has_value() &&
                                   result.model->parts.size() == 2;
        });
    }
    for (auto& caller : callers)
        caller.join();
    if (!std::ranges::all_of(concurrent_ok, [](bool value) { return value; })) {
        std::cerr << "shared incremental compiler was not thread safe\n";
        return 1;
    }
    return 0;
}
