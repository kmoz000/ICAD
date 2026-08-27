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

constexpr std::string_view shape_source = R"(PROJECT shape_incremental
UNITS mm
PARAMETER hole_radius 3 mm
BODY plate
SKETCH layout ON PLANE XY
SHAPE outer CLOSED ROLE STOCK
POINT p0 0 mm 0 mm FIXED
POINT p1 30 mm 0 mm FIXED
POINT p2 30 mm 20 mm FIXED
POINT p3 0 mm 20 mm FIXED
LINE e0 FROM p0 TO p1
LINE e1 FROM p1 TO p2
LINE e2 FROM p2 TO p3
LINE e3 FROM p3 TO p0
END
SHAPE hole CLOSED ROLE HOLE
POINT center 15 mm 10 mm FIXED
CIRCLE rim CENTER center RADIUS hole_radius
END
REGION perforated
OUTER outer
HOLES hole
END
SOLVE FULL
END
PAD stock FROM layout.perforated DEPTH 4 mm NEW
END
BODY stable
FEATURE block
TYPE BOX
WIDTH 5 mm
DEPTH 5 mm
HEIGHT 5 mm
ORIGIN_X 40 mm
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
    std::array<std::thread, 4> callers;
    for (std::size_t index = 0; index < callers.size(); ++index) {
        callers[index] = std::thread([&compiler, &concurrent_ok, index, &changed] {
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

    icad::compiler::IncrementalCompiler shape_compiler;
    const auto initial_shape = shape_compiler.compile(shape_source);
    if (!initial_shape.compilation.ok() ||
        initial_shape.incremental.recomputed_bodies.size() != 2) {
        std::cerr << "initial multi-shape incremental compile failed\n";
        return 1;
    }
    std::string changed_shape{shape_source};
    changed_shape.replace(changed_shape.find("PARAMETER hole_radius 3 mm"),
                          std::string_view{"PARAMETER hole_radius 3 mm"}.size(),
                          "PARAMETER hole_radius 4 mm");
    const auto updated_shape = shape_compiler.compile(changed_shape);
    if (!updated_shape.compilation.ok() ||
        updated_shape.incremental.recomputed_bodies != std::vector<std::string>{"plate"} ||
        updated_shape.incremental.reused_bodies != std::vector<std::string>{"stable"}) {
        std::cerr << "shape-profile edit did not invalidate only its dependent body\n";
        return 1;
    }
    return 0;
}
