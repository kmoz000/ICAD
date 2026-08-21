#include "icad/ai/inspector.hpp"
#include "icad/compiler/compiler.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

constexpr std::string_view valid_source = R"(PROJECT sketch_test
UNITS mm
PARAMETER width 40 mm
PARAMETER height 25 mm
ANGLE right_angle 90 deg
SKETCH outline
POINT a 0 mm 0 mm FIXED
POINT b 38 mm 2 mm
POINT c 39 mm 28 mm
POINT d -1 mm 24 mm
CONSTRAINT h1 HORIZONTAL a b
CONSTRAINT v1 VERTICAL b c
CONSTRAINT h2 HORIZONTAL c d
CONSTRAINT v2 VERTICAL d a
CONSTRAINT w1 DISTANCE a b width
CONSTRAINT l1 DISTANCE b c height
CONSTRAINT w2 DISTANCE c d width
CONSTRAINT l2 DISTANCE d a height
CONSTRAINT corner ANGLE a b c right_angle
END
BODY plate
FEATURE solid
TYPE EXTRUDE
PROFILE outline
HEIGHT 4 mm
END
END
)";

} // namespace

int main() {
    const auto compiled = icad::compiler::compile(valid_source);
    if (!compiled.ok() || compiled.ir_project->sketches.size() != 1 ||
        compiled.ir_project->profiles.size() != 1 ||
        !compiled.topology_model || compiled.topology_model->solids.size() != 1) {
        std::cerr << "fully constrained sketch did not compile\n";
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return 1;
    }
    const auto& sketch = compiled.ir_project->sketches.front();
    if (sketch.status != icad::compiler::ir::SketchSolveStatus::fully_constrained ||
        sketch.degrees_of_freedom != 0 || sketch.maximum_residual > 1e-6) {
        std::cerr << "sketch was not solved as fully constrained\n";
        return 1;
    }
    const auto& b = sketch.points[1].solved;
    const auto& c = sketch.points[2].solved;
    const auto& d = sketch.points[3].solved;
    if (std::abs(b.x_mm - 40.0) > 1e-5 || std::abs(b.y_mm) > 1e-5 ||
        std::abs(c.x_mm - 40.0) > 1e-5 || std::abs(c.y_mm - 25.0) > 1e-5 ||
        std::abs(d.x_mm) > 1e-5 || std::abs(d.y_mm - 25.0) > 1e-5) {
        std::cerr << "solved sketch coordinates are incorrect\n";
        return 1;
    }
    const auto inspection = icad::ai::project_json(*compiled.ir_project);
    if (!inspection.contains("\"sketches\":1") ||
        !inspection.contains("\"status\":\"fullyConstrained\"") ||
        !inspection.contains("\"targetReference\":\"width\"")) {
        std::cerr << "agent sketch inspection is incomplete\n";
        return 1;
    }

    constexpr std::string_view underconstrained = R"(PROJECT loose
UNITS mm
SKETCH line
POINT a 0 mm 0 mm FIXED
POINT b 5 mm 2 mm
CONSTRAINT horizontal HORIZONTAL a b
END
)";
    const auto loose = icad::compiler::compile(underconstrained);
    if (!loose.ok() || loose.ir_project->sketches.front().status !=
                           icad::compiler::ir::SketchSolveStatus::under_constrained ||
        loose.ir_project->sketches.front().degrees_of_freedom != 1) {
        std::cerr << "under-constrained sketch DOF was not reported\n";
        return 1;
    }

    constexpr std::string_view inconsistent = R"(PROJECT broken
UNITS mm
SKETCH line
POINT a 0 mm 0 mm FIXED
POINT b 5 mm 0 mm FIXED
CONSTRAINT impossible DISTANCE a b 10 mm
END
)";
    const auto broken = icad::compiler::compile(inconsistent);
    if (broken.ok() || broken.diagnostics.empty() ||
        broken.diagnostics.front().code != "ICAD-S0038") {
        std::cerr << "inconsistent sketch did not produce ICAD-S0038\n";
        return 1;
    }
    return 0;
}
