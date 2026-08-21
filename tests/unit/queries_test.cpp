#include "icad/cad/queries.hpp"
#include "icad/compiler/compiler.hpp"

#include <cmath>
#include <iostream>

namespace {

constexpr std::string_view source = R"(PROJECT queries
UNITS mm
TOLERANCE LINEAR 0.001 mm ANGULAR 0.01 deg
BODY first
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
END
BODY second
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 20 mm
END
END
)";

} // namespace

int main() {
    const auto compiled = icad::compiler::compile(source);
    if (!compiled.ok() || std::abs(compiled.ir_project->tolerance.linear_mm - 0.001) > 1e-12 ||
        std::abs(compiled.ir_project->tolerance.angular_degrees - 0.01) > 1e-12) {
        std::cerr << "tolerance policy did not lower\n";
        return 1;
    }
    const auto distance =
        icad::cad::exact_polyhedral_distance(*compiled.ir_project, "first", "second");
    if (!distance.found || std::abs(distance.distance_mm - 10.0) > 1e-9 ||
        std::abs(distance.first_point.x - 10.0) > 1e-9 ||
        std::abs(distance.second_point.x - 20.0) > 1e-9 ||
        distance.representation != "exactPolyhedral") {
        std::cerr << "exact polyhedral distance is incorrect\n";
        return 1;
    }
    const auto cut = icad::cad::section(*compiled.ir_project, {{5, 0, 0}, {1, 0, 0}}, "first");
    if (cut.segments.size() != 8 || cut.tolerance_mm != 0.001) {
        std::cerr << "plane section segment count is incorrect\n";
        return 1;
    }
    for (const auto& segment : cut.segments) {
        if (segment.body != "first" || std::abs(segment.segment.start.x - 5.0) > 1e-9 ||
            std::abs(segment.segment.end.x - 5.0) > 1e-9) {
            std::cerr << "plane section escaped the requested body or plane\n";
            return 1;
        }
    }
    const auto missing =
        icad::cad::exact_polyhedral_distance(*compiled.ir_project, "first", "missing");
    if (missing.found)
        return 1;
    return 0;
}
