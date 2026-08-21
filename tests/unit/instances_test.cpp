#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

constexpr std::string_view source = R"(PROJECT instances
UNITS mm
POINT3 origin 0 mm 0 mm 0 mm
POINT3 station_a 20 mm 0 mm 0 mm
POINT3 station_b 40 mm 0 mm 0 mm
VECTOR axis 0 0 1
BODY link_definition
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
INSTANCE link_a OF link_definition AT station_a ROTATION 0 deg 0 deg 0 deg
INSTANCE link_b OF link_definition AT station_b ROTATION 0 deg 0 deg 0 deg
JOINT mount FIXED WORLD link_a AT station_a AXIS axis
JOINT hinge REVOLUTE link_a link_b AT station_b AXIS axis VALUE 90 deg LIMIT -90 deg 90 deg
)";

} // namespace

int main() {
    const auto compiled = icad::compiler::compile(source);
    if (!compiled.ok() || compiled.ir_project->instances.size() != 2 ||
        !compiled.topology_model || compiled.topology_model->solids.size() != 3) {
        std::cerr << "component instances did not compile into three occurrences\n";
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return 1;
    }
    const auto analysis = icad::cad::analyze(*compiled.ir_project);
    if (analysis.parts.size() != 3 || std::abs(analysis.volume_mm3 - 3000.0) > 1e-6 ||
        std::abs(analysis.bounds.maximum[0] - 40.0) > 1e-6 ||
        std::abs(analysis.bounds.maximum[1] - 10.0) > 1e-6) {
        std::cerr << "instance transforms or metrics are incorrect\n";
        return 1;
    }
    const auto solved_link = std::ranges::find(analysis.parts, "link_b", &icad::cad::PartAnalysis::body);
    if (solved_link == analysis.parts.end() ||
        std::abs(solved_link->bounds.minimum[0] - 30.0) > 1e-6 ||
        std::abs(solved_link->bounds.maximum[0] - 40.0) > 1e-6 ||
        std::abs(solved_link->bounds.maximum[1] - 10.0) > 1e-6) {
        std::cerr << "revolute joint did not solve the instance pose\n";
        return 1;
    }
    const auto inspection = icad::ai::project_json(*compiled.ir_project);
    if (!inspection.contains("\"instances\":2") ||
        !inspection.contains("\"name\":\"link_b\",\"body\":\"link_definition\"") ||
        !inspection.contains("\"id\":\"instance:link_a\"")) {
        std::cerr << "agent instance inspection is incomplete\n";
        return 1;
    }
    return 0;
}
