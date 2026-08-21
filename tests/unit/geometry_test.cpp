#include "icad/compiler/compiler.hpp"
#include "icad/exchange/exporter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT GeometryTest
UNITS mm

BODY solids
FEATURE block
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
END
FEATURE post
TYPE CYLINDER
RADIUS 2 mm
HEIGHT 30 mm
ORIGIN_X 10 mm
ORIGIN_Y 5 mm
ORIGIN_Z 5 mm
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok()) {
        return fail("geometry fixture did not compile");
    }

    const auto output_root = std::filesystem::current_path() / "geometry-test-output";
    std::filesystem::create_directories(output_root);
    const auto step_path = output_root / "geometry.step";
    const auto obj_path = output_root / "geometry.obj";
    const auto gltf_path = output_root / "geometry.gltf";
    const auto glb_path = output_root / "geometry.glb";
    const auto three_mf_path = output_root / "geometry.3mf";

    const auto step = icad::exchange::export_project(*compilation.ir_project, step_path);
    if (!step.success || step.objects != 2 || !std::filesystem::exists(step_path)) {
        return fail("STEP exporter did not write two solids");
    }
    const auto inspection = icad::exchange::inspect_step(step_path);
    if (!inspection.success || inspection.roots != 2 || inspection.solids != 2) {
        return fail("STEP read-back did not recover two solids");
    }
    std::ifstream step_stream{step_path, std::ios::binary};
    const std::string step_content{std::istreambuf_iterator<char>{step_stream},
                                   std::istreambuf_iterator<char>{}};
    if (!step_content.contains("MANIFOLD_SOLID_BREP") ||
        !step_content.contains("ADVANCED_FACE") ||
        !step_content.contains("CYLINDRICAL_SURFACE") ||
        !step_content.contains("CIRCLE")) {
        return fail("STEP exporter omitted analytic B-Rep entities");
    }

    const auto obj = icad::exchange::export_project(*compilation.ir_project, obj_path);
    if (!obj.success || obj.objects != 2 || obj.vertices == 0 || obj.triangles == 0 ||
        !std::filesystem::exists(obj_path)) {
        return fail("OBJ exporter did not tessellate two ICAD solids");
    }
    const auto gltf = icad::exchange::export_project(*compilation.ir_project, gltf_path);
    const auto glb = icad::exchange::export_project(*compilation.ir_project, glb_path);
    const auto three_mf = icad::exchange::export_project(*compilation.ir_project, three_mf_path);
    const auto gltf_inspection = icad::exchange::inspect_gltf(gltf_path);
    const auto glb_inspection = icad::exchange::inspect_gltf(glb_path);
    const auto three_mf_inspection = icad::exchange::inspect_3mf(three_mf_path);
    if (!gltf.success || !glb.success || !three_mf.success || !gltf_inspection.success ||
        !glb_inspection.success || !three_mf_inspection.success ||
        gltf_inspection.objects != 2 || glb_inspection.objects != 2 ||
        three_mf_inspection.objects != 2) {
        return fail("glTF/GLB/3MF export or structural read-back failed");
    }
    return 0;
}
