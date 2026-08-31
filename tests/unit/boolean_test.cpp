#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/model.hpp"
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
PROJECT BooleanTest
UNITS mm
BODY union_result
FEATURE base
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
END
FEATURE addition
TYPE BOX
OPERATION UNION
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 5 mm
END
END
BODY cut_result
FEATURE stock
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 30 mm
END
FEATURE opening
TYPE BOX
OPERATION CUT
WIDTH 4 mm
DEPTH 4 mm
HEIGHT 12 mm
ORIGIN_X 33 mm
ORIGIN_Y 3 mm
ORIGIN_Z -1 mm
END
END
BODY intersect_result
FEATURE first
TYPE BOX
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 60 mm
END
FEATURE overlap
TYPE BOX
OPERATION INTERSECT
WIDTH 10 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 65 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result,
                            std::string_view code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok() || !compilation.topology_model)
        return fail("native boolean fixture did not compile");
    const auto& project = *compilation.ir_project;
    if (project.bodies.size() != 3 ||
        project.bodies[0].features[1].operation != icad::compiler::ir::FeatureOperation::unite ||
        project.bodies[1].features[1].operation != icad::compiler::ir::FeatureOperation::cut ||
        project.bodies[2].features[1].operation !=
            icad::compiler::ir::FeatureOperation::intersect) {
        return fail("boolean operations were not preserved in canonical IR");
    }

    const auto metrics = icad::cad::analyze(project);
    if (metrics.parts.size() != 3 || std::abs(metrics.volume_mm3 - 2840.0) > 1e-7 ||
        metrics.bounds.minimum[0] != 0.0 || metrics.bounds.maximum[0] != 70.0) {
        return fail("boolean reconstruction produced incorrect volume or bounds");
    }
    const auto topology_validation = icad::cad::validate_topology(*compilation.topology_model);
    if (!topology_validation.valid() || compilation.topology_model->solids.size() != 3 ||
        compilation.topology_model->solids[0].euler_characteristic() != 2 ||
        compilation.topology_model->solids[1].euler_characteristic() != 0 ||
        compilation.topology_model->solids[2].euler_characteristic() != 2) {
        return fail("boolean reconstruction did not preserve closed-shell topology and cut genus");
    }

    const auto inspection = icad::ai::project_json(project);
    if (!inspection.contains("\"booleanOperations\":3") ||
        !inspection.contains("conforming boolean boundary splits") ||
        !inspection.contains("\"modeling\":{\"operations\":[]") ||
        !inspection.contains("\"repairs\":[")) {
        return fail("agent inspection omitted boolean operation or repair provenance");
    }

    const auto output_root = std::filesystem::current_path() / "boolean-test-output";
    std::filesystem::create_directories(output_root);
    const auto step_path = output_root / "boolean.step";
    const auto stl_path = output_root / "boolean.stl";
    const auto obj_path = output_root / "boolean.obj";
    const auto step = icad::exchange::export_project(project, step_path);
    const auto stl = icad::exchange::export_project(project, stl_path);
    const auto obj = icad::exchange::export_project(project, obj_path);
    const auto step_readback = icad::exchange::inspect_step(step_path);
    const auto stl_readback = icad::exchange::inspect_stl(stl_path);
    if (!step.success || !stl.success || !obj.success || step.objects != 3 || stl.objects != 3 ||
        obj.objects != 3 || !step_readback.success || step_readback.solids != 3 ||
        !stl_readback.success || stl_readback.solids != 3) {
        return fail("boolean STEP/STL/OBJ export or structural read-back failed");
    }

    const auto invalid_first = icad::compiler::compile(
        "PROJECT invalid_boolean\nUNITS mm\nBODY b\nFEATURE f\nTYPE BOX\n"
        "OPERATION CUT\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n");
    if (invalid_first.ok() || !has_code(invalid_first, "ICAD-S0034"))
        return fail("semantic analysis accepted a boolean operation without a base feature");

    const auto invalid_name = icad::compiler::compile(
        "PROJECT invalid_operation\nUNITS mm\nBODY b\nFEATURE f\nTYPE BOX\n"
        "OPERATION MERGE\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n");
    if (invalid_name.ok() || !has_code(invalid_name, "ICAD-S0034"))
        return fail("semantic analysis accepted an unknown boolean operation");

    const auto disjoint = icad::compiler::compile(
        "PROJECT disjoint\nUNITS mm\nBODY b\nFEATURE a\nTYPE BOX\n"
        "WIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nFEATURE c\nTYPE BOX\n"
        "OPERATION UNION\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nORIGIN_X 2 mm\n"
        "END\nEND\n");
    if (!disjoint.ok() || !disjoint.topology_model || disjoint.topology_model->solids.size() != 2 ||
        !icad::cad::validate_topology(*disjoint.topology_model).valid()) {
        return fail("disconnected union was not separated into two valid solids");
    }

    const auto curved_cut = icad::compiler::compile(
        "PROJECT curved\nUNITS mm\nBODY b\nFEATURE stock\nTYPE BOX\n"
        "WIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\nEND\nFEATURE hole\nTYPE CYLINDER\n"
        "OPERATION CUT\nRADIUS 2 mm\nHEIGHT 12 mm\nORIGIN_X 5 mm\nORIGIN_Y 5 mm\n"
        "ORIGIN_Z -1 mm\nEND\nEND\n");
    const double analytic_curved_cut_volume = 1000.0 - std::numbers::pi * 2.0 * 2.0 * 10.0;
    if (!curved_cut.ok() ||
        std::abs(icad::cad::analyze(*curved_cut.ir_project).volume_mm3 -
                 analytic_curved_cut_volume) > analytic_curved_cut_volume * 0.001) {
        return fail("high-resolution curved-operand cut did not converge to analytic volume");
    }

    const auto twisted_blade_union = icad::compiler::compile(R"ICAD(
PROJECT twisted_blade_union
UNITS mm
PROFILE airfoil_root
POINT 0 mm 0 mm
POINT 3 mm -4.8 mm
POINT 12 mm -6.2 mm
POINT 24 mm -3.2 mm
POINT 28 mm 0 mm
POINT 22 mm 3.8 mm
POINT 10 mm 5.6 mm
POINT 2 mm 3.2 mm
END
PROFILE airfoil_tip
POINT 2 mm 0 mm
POINT 5 mm -3.2 mm
POINT 12 mm -4.2 mm
POINT 22 mm -2.4 mm
POINT 25 mm 0 mm
POINT 20 mm 2.7 mm
POINT 10 mm 3.8 mm
POINT 3.5 mm 2.1 mm
END
PROFILE root_base
POINT 0 mm 0 mm
POINT 12 mm 0 mm
POINT 14 mm 2 mm
POINT 12.5 mm 4 mm
POINT 13.5 mm 6 mm
POINT 11 mm 8 mm
POINT 3 mm 8 mm
POINT 0.5 mm 6 mm
POINT 1.5 mm 4 mm
POINT 0 mm 2 mm
END
PROFILE root_neck
POINT 2 mm 1 mm
POINT 10 mm 1 mm
POINT 11.5 mm 2.5 mm
POINT 10.5 mm 4 mm
POINT 11 mm 5.5 mm
POINT 9.5 mm 7 mm
POINT 4 mm 7 mm
POINT 2.5 mm 5.5 mm
POINT 3 mm 4 mm
POINT 2 mm 2.5 mm
END
BODY blade
FEATURE captured_root
TYPE LOFT
PROFILE root_base
TARGET_PROFILE root_neck
HEIGHT 12 mm
ORIGIN_X 2 mm
ORIGIN_Z -6 mm
END
FEATURE twisted_airfoil
TYPE FREEFORM
PROFILE airfoil_root
TARGET_PROFILE airfoil_tip
OPERATION UNION
HEIGHT 76 mm
TWIST 26 deg
COUNT 9
END
END
)ICAD");
    if (!twisted_blade_union.ok() || !twisted_blade_union.topology_model ||
        twisted_blade_union.topology_model->solids.size() != 1 ||
        !icad::cad::validate_topology(*twisted_blade_union.topology_model).valid()) {
        return fail("twisted multi-point airfoil did not form one closed solid with its root");
    }
    const auto blade_model = icad::cad::build_model(*twisted_blade_union.ir_project);
    if (blade_model.parts.size() != 1 ||
        std::ranges::none_of(blade_model.parts.front().repairs, [](const auto& repair) {
            return repair.contains("sub-micron freeform-union fragments");
        })) {
        return fail("freeform root-union repair provenance was not preserved");
    }

    const auto batched_radial_cuts = icad::compiler::compile(
        "PROJECT batched_radial_cuts\nUNITS mm\nBODY plate\n"
        "FEATURE stock\nTYPE BOX\nWIDTH 20 mm\nDEPTH 20 mm\nHEIGHT 10 mm\nEND\n"
        "FEATURE hole_a\nTYPE CYLINDER\nOPERATION CUT\nRADIUS 2 mm\nHEIGHT 12 mm\n"
        "ORIGIN_X 5 mm\nORIGIN_Y 10 mm\nORIGIN_Z -1 mm\nEND\n"
        "FEATURE hole_b\nTYPE CYLINDER\nOPERATION CUT\nRADIUS 2 mm\nHEIGHT 12 mm\n"
        "ORIGIN_X 15 mm\nORIGIN_Y 10 mm\nORIGIN_Z -1 mm\nEND\nEND\n");
    if (!batched_radial_cuts.ok())
        return fail("compatible transformed primitive cutters did not compile");
    const auto batched_model = icad::cad::build_model(*batched_radial_cuts.ir_project);
    if (!icad::cad::is_valid(batched_model) || batched_model.parts.size() != 1 ||
        std::ranges::none_of(batched_model.parts.front().repairs, [](const auto& repair) {
            return repair.contains("batched 2 compatible cut features");
        })) {
        return fail("transformed primitive cutters were not evaluated in one boolean transaction");
    }

    const auto empty_intersection = icad::compiler::compile(
        "PROJECT empty\nUNITS mm\nBODY b\nFEATURE a\nTYPE BOX\n"
        "WIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nFEATURE c\nTYPE BOX\n"
        "OPERATION INTERSECT\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\nORIGIN_X 2 mm\n"
        "END\nEND\n");
    if (empty_intersection.ok() || !has_code(empty_intersection, "ICAD-G0003"))
        return fail("empty intersection did not produce an explicit geometry diagnostic");
    return 0;
}
