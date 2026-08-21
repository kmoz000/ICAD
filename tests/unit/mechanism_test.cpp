#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/constraints/validator.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result, std::string_view code)
    -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

auto main() -> int {
    const auto source = R"ICAD(PROJECT mechanism
UNITS mm
PARAMETER anchor_x 20 mm
ANGLE drive 30 deg
POINT3 origin 0 mm 0 mm 0 mm
POINT3 anchor anchor_x 0 mm 0 mm
VECTOR x_axis 2 0 0
VECTOR z_axis 0 0 1
POINT3 link_tip FROM anchor ALONG x_axis DISTANCE 10 mm
POINT3 target FROM anchor ALONG x_axis DISTANCE 10 mm
POINT3 swing_tip FROM anchor ALONG swing_axis DISTANCE 10 mm
VECTOR link_axis FROM anchor TO link_tip
VECTOR swing_axis ROTATE x_axis AROUND z_axis BY drive
POSE base AT origin ROTATION 0 deg 0 deg 0 deg
POSE arm AT anchor ROTATION 0 deg 0 deg 90 deg
JOINT ground FIXED WORLD base AT origin AXIS z_axis
JOINT hinge REVOLUTE base arm AT anchor AXIS z_axis VALUE drive LIMIT -45 deg 90 deg
CONSTRAINT axes PERPENDICULAR x_axis z_axis
CONSTRAINT derived_axis PARALLEL link_axis x_axis
CONSTRAINT tip_target COINCIDENT link_tip target 0.001 mm
CONSTRAINT swing_angle ANGLE_BETWEEN x_axis swing_axis drive
BODY base
FEATURE block
TYPE BOX
WIDTH 2 mm
DEPTH 2 mm
HEIGHT 2 mm
END
END
BODY arm
FEATURE link
TYPE BOX
WIDTH 10 mm
DEPTH 2 mm
HEIGHT 2 mm
END
END
)ICAD";
    const auto compiled = icad::compiler::compile(source);
    if (!compiled.ok()) {
        std::cerr << "typed mechanism fixture did not compile\n";
        return 1;
    }
    const auto& project = *compiled.ir_project;
    if (project.angles.size() != 1 || project.points.size() != 5 || project.vectors.size() != 4 ||
        project.poses.size() != 2 || project.joints.size() != 2 ||
        std::abs(project.vectors.front().unit[0] - 1.0) > 1e-12 ||
        project.vectors.back().kind != icad::compiler::ir::DirectionKind::rotated ||
        std::abs(project.vectors.back().unit[0] - std::sqrt(3.0) * 0.5) > 1e-12 ||
        std::abs(project.vectors.back().unit[1] - 0.5) > 1e-12 ||
        project.points.back().kind != icad::compiler::ir::SpatialPointKind::offset ||
        std::abs(project.points.back().position_mm[0] - (20.0 + 5.0 * std::sqrt(3.0))) > 1e-12 ||
        std::abs(project.points.back().position_mm[1] - 5.0) > 1e-12 ||
        std::abs(project.joints.back().value - 30.0) > 1e-12) {
        std::cerr << "mechanism declarations were not lowered canonically\n";
        return 1;
    }
    const auto analysis = icad::cad::analyze(project);
    if (std::abs(analysis.bounds.maximum[0] - 20.0) > 1e-9 ||
        std::abs(analysis.bounds.maximum[1] - 10.0) > 1e-9) {
        std::cerr << "body POSE was not applied to delivered geometry\n";
        return 1;
    }
    const auto topology = icad::cad::build_topology(project);
    double maximum_y = topology.solids.back().vertices.front().point.y;
    for (const auto& vertex : topology.solids.back().vertices) {
        maximum_y = std::max(maximum_y, vertex.point.y);
    }
    if (!icad::cad::validate_topology(topology).valid() || std::abs(maximum_y - 10.0) > 1e-9) {
        std::cerr << "body POSE was not applied to exact topology\n";
        return 1;
    }
    const auto constraints = icad::constraints::validate(project);
    if (constraints.size() != 4 || !icad::constraints::all_passed(constraints) ||
        std::abs(constraints.back().required_mm - 30.0) > 1e-12 ||
        project.constraints.back().target_reference != "drive") {
        std::cerr << "spatial constraints did not validate\n";
        return 1;
    }
    const auto inspection = icad::ai::project_json(project);
    if (!inspection.contains("\"degreesOfFreedom\":1") ||
        !inspection.contains("\"type\":\"revolute\"") ||
        !inspection.contains("\"kind\":\"rotated\"") ||
        !inspection.contains("\"source\":\"x_axis\",\"around\":\"z_axis\",\"angleDeg\":30,\"angleReference\":\"drive\"") ||
        !inspection.contains("\"from\":\"anchor\",\"along\":\"swing_axis\"") ||
        !inspection.contains("\"targetReference\":\"drive\"") ||
        !inspection.contains("\"geometry\":{\"boundsMin\":")) {
        std::cerr << "agent inspection omitted resolved mechanism state\n";
        return 1;
    }

    const auto spatial_cycle = icad::compiler::compile(
        "PROJECT spatial_cycle\nUNITS mm\nANGLE a 10 deg\nVECTOR z 0 0 1\n"
        "VECTOR first ROTATE second AROUND z BY a\n"
        "VECTOR second ROTATE first AROUND z BY a\n"
        "BODY part\nFEATURE f\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n");
    if (spatial_cycle.ok() || !has_code(spatial_cycle, "ICAD-S0031")) {
        std::cerr << "cyclic spatial expression graph was accepted\n";
        return 1;
    }

    const auto outside_limit = icad::compiler::compile(
        "PROJECT invalid\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\nVECTOR z 0 0 1\n"
        "BODY a\nFEATURE f\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n"
        "JOINT j REVOLUTE WORLD a AT p AXIS z VALUE 100 deg LIMIT -20 deg 20 deg\n");
    if (outside_limit.ok() || !has_code(outside_limit, "ICAD-S0033")) {
        std::cerr << "out-of-range joint value was accepted\n";
        return 1;
    }

    const auto cycle = icad::compiler::compile(
        "PROJECT cycle\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\nVECTOR z 0 0 1\n"
        "BODY a\nFEATURE fa\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n"
        "BODY b\nFEATURE fb\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n"
        "JOINT ab FIXED a b AT p AXIS z\nJOINT ba FIXED b a AT p AXIS z\n");
    if (cycle.ok() || !has_code(cycle, "ICAD-S0033")) {
        std::cerr << "cyclic mechanism graph was accepted\n";
        return 1;
    }
    return 0;
}
