#include "icad/cad/analysis.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/exchange/exporter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT native_profiles
UNITS mm
PARAMETER plate_height 12 mm

PROFILE l_shape
POINT 0 mm 0 mm
POINT 100 mm 0 mm
POINT 100 mm 40 mm
POINT 60 mm 40 mm
POINT 60 mm 80 mm
POINT 0 mm 80 mm
END

PROFILE ring_section
POINT 20 mm -5 mm
POINT 30 mm -5 mm
POINT 30 mm 5 mm
POINT 20 mm 5 mm
END

PROFILE rounded_plate
START 10 mm 0 mm
LINE 40 mm 0 mm
ARC 50 mm 10 mm CENTER 40 mm 10 mm CCW
LINE 50 mm 20 mm
ARC 40 mm 30 mm CENTER 40 mm 20 mm CCW
LINE 10 mm 30 mm
ARC 0 mm 20 mm CENTER 10 mm 20 mm CCW
LINE 0 mm 10 mm
ARC 10 mm 0 mm CENTER 10 mm 10 mm CCW
CLOSE
END

PROFILE circular_post
CIRCLE 0 mm 0 mm 8 mm
END

BODY profiles
FEATURE plate
TYPE EXTRUDE
PROFILE l_shape
HEIGHT plate_height
END
FEATURE ring
TYPE REVOLVE
PROFILE ring_section
ANGLE 360 deg
ORIGIN_X 150 mm
END
FEATURE rounded
TYPE EXTRUDE
PROFILE rounded_plate
HEIGHT 5 mm
ORIGIN_X 250 mm
END
FEATURE post
TYPE EXTRUDE
PROFILE circular_post
HEIGHT 20 mm
ORIGIN_X 350 mm
END
END
)ICAD";

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result, std::string_view code)
    -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& issue) { return issue.code == code; });
}

} // namespace

auto main() -> int {
    const auto result = icad::compiler::compile(source);
    if (!result.ok() || result.ir_project->profiles.size() != 4 ||
        result.ir_project->bodies.front().features.size() != 4) {
        std::cerr << "native profiles did not compile\n";
        return 1;
    }
    const auto& rounded_profile = result.ir_project->profiles[2];
    const auto& circle_profile = result.ir_project->profiles[3];
    if (rounded_profile.segments.size() != 8 || rounded_profile.points.size() != 36 ||
        std::ranges::count(rounded_profile.segments,
                           icad::compiler::ir::ProfileSegmentKind::circular_arc,
                           &icad::compiler::ir::ProfileSegment::kind) != 4 ||
        circle_profile.segments.size() != 1 || circle_profile.points.size() != 32 ||
        circle_profile.segments.front().kind !=
            icad::compiler::ir::ProfileSegmentKind::circular_arc) {
        std::cerr << "analytic profile curves did not lower deterministically\n";
        return 1;
    }
    const auto topology = icad::cad::build_topology(*result.ir_project);
    const auto rounded_solid =
        std::ranges::find(topology.solids, "rounded", &icad::cad::SolidTopology::feature);
    const auto post_solid =
        std::ranges::find(topology.solids, "post", &icad::cad::SolidTopology::feature);
    if (rounded_solid == topology.solids.end() || post_solid == topology.solids.end() ||
        rounded_solid->edges.size() != 24 || rounded_solid->faces.size() != 10 ||
        std::ranges::count_if(rounded_solid->faces,
                              [](const auto& face) {
                                  return face.surface.kind == icad::cad::SurfaceKind::cylinder;
                              }) != 4 ||
        post_solid->edges.size() != 3 || post_solid->faces.size() != 3 ||
        !icad::cad::validate_topology(topology).valid()) {
        std::cerr << "analytic profile topology is incomplete or invalid\n";
        return 1;
    }
    const auto metrics = icad::cad::analyze(*result.ir_project);
    const double rounded_area = 1100.0 + 100.0 * std::numbers::pi;
    const double rounded_perimeter = 80.0 + 20.0 * std::numbers::pi;
    if (metrics.parts.size() != 4 ||
        std::abs(metrics.parts[1].volume_mm3 - 5000.0 * std::numbers::pi) > 1e-8 ||
        std::abs(metrics.parts[2].volume_mm3 - rounded_area * 5.0) > 1e-8 ||
        std::abs(metrics.parts[2].surface_area_mm2 -
                 (2.0 * rounded_area + rounded_perimeter * 5.0)) > 1e-8 ||
        std::abs(metrics.parts[3].volume_mm3 - 1280.0 * std::numbers::pi) > 1e-8) {
        std::cerr << "analytic profile area or volume is inaccurate\n";
        return 1;
    }
    const auto& height =
        result.ir_project->bodies.front().features.front().properties.front().value;
    if (height.value != 12.0 || height.unit != "mm") {
        std::cerr << "parameter reference did not lower into feature property\n";
        return 1;
    }

    const auto root = std::filesystem::current_path() / "profile-test-output";
    const auto step = icad::exchange::export_project(*result.ir_project, root / "profiles.step");
    const auto obj = icad::exchange::export_project(*result.ir_project, root / "profiles.obj");
    const auto inspection = icad::exchange::inspect_step(root / "profiles.step");
    if (!step.success || !obj.success || step.objects != 4 || obj.objects != 4 ||
        obj.vertices == 0 || obj.triangles == 0 || !inspection.success || inspection.solids != 4) {
        std::cerr << "native profile geometry did not produce four valid closed solids\n";
        return 1;
    }

    const auto invalid = icad::compiler::compile(
        "PROJECT crossed\nUNITS mm\nPROFILE bow\nPOINT 0 mm 0 mm\nPOINT 10 mm 10 mm\n"
        "POINT 0 mm 10 mm\nPOINT 10 mm 0 mm\nEND\n");
    if (invalid.ok()) {
        std::cerr << "self-intersecting profile was accepted\n";
        return 1;
    }
    const auto overlapping =
        icad::compiler::compile("PROJECT overlap\nUNITS mm\nPROFILE backtrack\nPOINT 0 mm 0 mm\n"
                                "POINT 10 mm 0 mm\nPOINT 5 mm 0 mm\nPOINT 5 mm 10 mm\n"
                                "POINT 0 mm 10 mm\nEND\n");
    if (overlapping.ok() || !has_code(overlapping, "ICAD-S0020")) {
        std::cerr << "overlapping profile boundary was accepted\n";
        return 1;
    }
    const auto bad_arc = icad::compiler::compile(
        "PROJECT bad_arc\nUNITS mm\nPROFILE bad\nSTART 0 mm 0 mm\nLINE 10 mm 0 mm\n"
        "ARC 20 mm 10 mm CENTER 0 mm 0 mm CCW\nLINE 0 mm 10 mm\nCLOSE\nEND\n");
    if (bad_arc.ok() || !has_code(bad_arc, "ICAD-S0029")) {
        std::cerr << "geometrically inconsistent arc was accepted\n";
        return 1;
    }
    const auto clockwise = icad::compiler::compile(
        "PROJECT clockwise\nUNITS mm\nPROFILE rounded\nSTART 10 mm 0 mm\n"
        "ARC 0 mm 10 mm CENTER 10 mm 10 mm CW\nLINE 0 mm 20 mm\n"
        "ARC 10 mm 30 mm CENTER 10 mm 20 mm CW\nLINE 40 mm 30 mm\n"
        "ARC 50 mm 20 mm CENTER 40 mm 20 mm CW\nLINE 50 mm 10 mm\n"
        "ARC 40 mm 0 mm CENTER 40 mm 10 mm CW\nLINE 10 mm 0 mm\nCLOSE\nEND\n"
        "BODY b\nFEATURE plate\nTYPE EXTRUDE\nPROFILE rounded\nHEIGHT 2 mm\nEND\nEND\n");
    if (!clockwise.ok() ||
        std::ranges::any_of(clockwise.ir_project->profiles.front().segments,
                            [](const auto& segment) { return segment.sweep_radians < 0.0; }) ||
        !icad::cad::validate_topology(*clockwise.topology_model).valid()) {
        std::cerr << "clockwise path was not normalized into valid canonical winding\n";
        return 1;
    }
    const auto curved_revolve = icad::compiler::compile(
        "PROJECT curved_revolve\nUNITS mm\nPROFILE disc\nCIRCLE 20 mm 0 mm 5 mm\nEND\n"
        "BODY b\nFEATURE torus\nTYPE REVOLVE\nPROFILE disc\nANGLE 360 deg\nEND\nEND\n");
    if (!curved_revolve.ok() || !curved_revolve.topology_model ||
        !icad::cad::validate_topology(*curved_revolve.topology_model).valid() ||
        curved_revolve.topology_model->solids.front().euler_characteristic() != 0 ||
        std::abs(icad::cad::analyze(*curved_revolve.ir_project).volume_mm3 -
                 9743.419838555252) > 1e-6) {
        std::cerr << "curved revolve did not produce a validated faceted torus\n";
        return 1;
    }
    return 0;
}
