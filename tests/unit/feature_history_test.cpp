#include "icad/cad/analysis.hpp"
#include "icad/cad/model.hpp"
#include "icad/ai/inspector.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/compiler/dependency_graph.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view history = R"ICAD(PROJECT history
UNITS mm
PARAMETER base_depth 12 mm
PARAMETER bore_depth 20 mm
BODY bracket
SKETCH base ON PLANE XY
POINT p0 0 mm 0 mm FIXED
POINT p1 100 mm 0 mm FIXED
POINT p2 100 mm 60 mm FIXED
POINT p3 0 mm 60 mm FIXED
LINE bottom FROM p0 TO p1
LINE right FROM p1 TO p2
LINE top FROM p2 TO p3
LINE left FROM p3 TO p0
END
PAD base_solid FROM base DEPTH base_depth NEW
SKETCH boss ON FACE base_solid Z_MAX
POINT p0 25 mm 15 mm FIXED
POINT p1 75 mm 15 mm FIXED
POINT p2 75 mm 45 mm FIXED
POINT p3 25 mm 45 mm FIXED
END
PAD raised_boss FROM boss DEPTH 20 mm ADD
SKETCH bore ON FACE raised_boss Z_MAX
CIRCLE 50 mm 30 mm 8 mm
END
POCKET mounting_bore FROM bore DEPTH bore_depth
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto result = icad::compiler::compile(history);
    if (!result.ok()) {
        for (const auto& diagnostic : result.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return fail("sketch-first feature history did not compile");
    }
    if (result.program->bodies.size() != 1 || result.program->bodies.front().sketches.size() != 3 ||
        result.ir_project->sketches.size() != 3 || result.ir_project->profiles.size() != 3 ||
        result.ir_project->bodies.front().features.size() != 3) {
        return fail("history syntax did not lower to three sketches and three operations");
    }
    if (result.ir_project->sketches.front().entities.size() != 4 ||
        result.ir_project->profiles.front().segments.size() != 4) {
        return fail("named sketch entities were not preserved as stable profile topology");
    }
    const auto& features = result.ir_project->bodies.front().features;
    if (features[0].source_keyword != "PAD" || features[1].source_keyword != "PAD" ||
        features[2].source_keyword != "POCKET" || features[0].profile != "bracket::base" ||
        features[1].profile != "bracket::boss" || features[2].profile != "bracket::bore" ||
        result.ir_project->sketches.front().body != "bracket" ||
        features[0].operation != icad::compiler::ir::FeatureOperation::create ||
        features[1].operation != icad::compiler::ir::FeatureOperation::unite ||
        features[1].support_feature != "base_solid" || features[1].support_face != "Z_MAX" ||
        features[2].operation != icad::compiler::ir::FeatureOperation::cut ||
        features[2].support_feature != "raised_boss" || features[2].support_face != "Z_MAX") {
        return fail("PAD and POCKET history semantics are incomplete");
    }
    const auto model = icad::cad::build_model(*result.ir_project);
    if (!icad::cad::is_valid(model) || model.parts.empty())
        return fail("face-attached history did not produce valid native geometry");
    const auto measurements = icad::cad::analyze(*result.ir_project);
    if (measurements.bounds.minimum[0] < -1e-6 || measurements.bounds.minimum[1] < -1e-6 ||
        measurements.bounds.minimum[2] < -1e-6 || measurements.bounds.maximum[0] < 99.9 ||
        measurements.bounds.maximum[1] < 59.9 || measurements.bounds.maximum[2] < 31.9 ||
        measurements.bounds.maximum[2] > 32.1 || measurements.volume_mm3 < 95000.0) {
        return fail("face-attached history lost the base solid or its full envelope");
    }
    const auto visual = icad::ai::visual_snapshot_json(*result.ir_project);
    if (!visual.contains("\"featureHistory\":[") ||
        !visual.contains("\"command\":\"POCKET\"") ||
        !visual.contains("\"sketch\":\"bore\",\"sketchId\":\"bracket::bore\"") ||
        !visual.contains("\"supportFeature\":\"raised_boss\"") ||
        !visual.contains("\"operation\":\"CUT\"")) {
        return fail("visual.json omitted the agent-readable feature history");
    }
    const auto dependencies = icad::compiler::build_dependency_graph(*result.ir_project);
    const auto find_node = [&dependencies](std::string_view id) {
        return std::find_if(dependencies.nodes.begin(), dependencies.nodes.end(),
                            [id](const auto& node) { return node.id == id; });
    };
    const auto has_dependency = [](const auto& node, std::string_view id) {
        return std::find(node.dependencies.begin(), node.dependencies.end(), id) !=
               node.dependencies.end();
    };
    const auto boss_sketch = find_node("sketch:bracket::boss");
    const auto bore_sketch = find_node("sketch:bracket::bore");
    if (boss_sketch == dependencies.nodes.end() || bore_sketch == dependencies.nodes.end() ||
        !has_dependency(*boss_sketch, "feature:bracket/base_solid") ||
        !has_dependency(*bore_sketch, "feature:bracket/raised_boss")) {
        return fail("face sketches do not depend on their supporting history feature");
    }
    const auto bad_order = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY part\n"
        "SKETCH boss ON FACE missing Z_MAX\nCIRCLE 0 mm 0 mm 2 mm\nEND\n"
        "PAD boss FROM boss DEPTH 2 mm ADD\nEND\n");
    if (bad_order.ok())
        return fail("history accepted a sketch attached to a future or missing feature");
    const auto mixed_circle = icad::compiler::compile(
        "PROJECT bad_circle\nUNITS mm\nBODY part\n"
        "SKETCH ring ON PLANE XY\nCIRCLE 0 mm 0 mm 2 mm\n"
        "CONSTRAINT width DISTANCE p0 p1 4 mm\nEND\n"
        "PAD solid FROM ring DEPTH 2 mm NEW\nEND\n");
    if (mixed_circle.ok())
        return fail("circle sketch accepted incompatible point constraints");
    const auto detached_face = icad::compiler::compile(
        "PROJECT detached\nUNITS mm\nSKETCH orphan ON FACE solid Z_MAX\n"
        "CIRCLE 0 mm 0 mm 2 mm\nEND\n");
    if (detached_face.ok())
        return fail("face-attached sketch was accepted outside a BODY history");
    const auto implicit_datum = icad::compiler::compile(
        "PROJECT implicit\nUNITS mm\nBODY part\nSKETCH base\n"
        "CIRCLE 0 mm 0 mm 2 mm\nEND\nPAD solid FROM base DEPTH 2 mm NEW\nEND\n");
    if (implicit_datum.ok())
        return fail("BODY sketch accepted an implicit datum plane");
    const auto unused_sketch = icad::compiler::compile(
        "PROJECT unused\nUNITS mm\nBODY part\nSKETCH base ON PLANE XY\n"
        "CIRCLE 0 mm 0 mm 2 mm\nEND\nEND\n");
    if (unused_sketch.ok())
        return fail("BODY accepted an unused construction sketch without explicit semantics");
    const auto open_chain = icad::compiler::compile(
        "PROJECT open_chain\nUNITS mm\nBODY part\nSKETCH base ON PLANE XY\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 10 mm 0 mm FIXED\n"
        "POINT p2 10 mm 10 mm FIXED\nLINE e0 FROM p0 TO p1\n"
        "LINE e1 FROM p2 TO p0\nEND\nPAD solid FROM base DEPTH 2 mm NEW\nEND\n");
    if (open_chain.ok())
        return fail("open explicit sketch boundary was accepted");
    const auto future_sketch = icad::compiler::compile(
        "PROJECT future_sketch\nUNITS mm\nBODY part\n"
        "FEATURE solid\nTYPE EXTRUDE\nPROFILE base\nHEIGHT 2 mm\nEND\n"
        "SKETCH base ON PLANE XY\nCIRCLE 0 mm 0 mm 2 mm\nEND\nEND\n");
    if (future_sketch.ok())
        return fail("BODY accepted an operation that consumes a later sketch");

    const auto scoped = icad::compiler::compile(
        "PROJECT scoped\nUNITS mm\n"
        "BODY left\nSKETCH base ON PLANE XY\nCIRCLE 0 mm 0 mm 4 mm\nEND\n"
        "PAD solid FROM base DEPTH 5 mm NEW\nEND\n"
        "BODY right\nSKETCH base ON PLANE YZ\nCIRCLE 0 mm 0 mm 3 mm\nEND\n"
        "PAD solid FROM base DEPTH 7 mm NEW\nEND\n");
    if (!scoped.ok() || scoped.ir_project->profiles.size() != 2 ||
        scoped.ir_project->profiles[0].name != "left::base" ||
        scoped.ir_project->profiles[1].name != "right::base" ||
        scoped.ir_project->bodies[0].features[0].profile != "left::base" ||
        scoped.ir_project->bodies[1].features[0].profile != "right::base") {
        return fail("body-local sketch names are not independently scoped");
    }

    const auto xz = icad::compiler::compile(
        "PROJECT xz_part\nUNITS mm\nBODY rib\nSKETCH rib_profile ON PLANE XZ\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 30 mm 0 mm FIXED\n"
        "POINT p2 30 mm 20 mm FIXED\nPOINT p3 0 mm 20 mm FIXED\nEND\n"
        "PAD rib_solid FROM rib_profile DEPTH 10 mm NEW\nEND\n");
    const auto yz = icad::compiler::compile(
        "PROJECT yz_part\nUNITS mm\nBODY rib\nSKETCH rib_profile ON PLANE YZ\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 30 mm 0 mm FIXED\n"
        "POINT p2 30 mm 20 mm FIXED\nPOINT p3 0 mm 20 mm FIXED\nEND\n"
        "PAD rib_solid FROM rib_profile DEPTH 10 mm NEW\nEND\n");
    if (!xz.ok() || !yz.ok())
        return fail("XZ or YZ datum history did not compile");
    const auto xz_bounds = icad::cad::analyze(*xz.ir_project).bounds;
    const auto yz_bounds = icad::cad::analyze(*yz.ir_project).bounds;
    if (xz_bounds.maximum[0] != 30.0 || xz_bounds.maximum[1] != 10.0 ||
        xz_bounds.maximum[2] != 20.0 || yz_bounds.maximum[0] != 10.0 ||
        yz_bounds.maximum[1] != 30.0 || yz_bounds.maximum[2] != 20.0) {
        return fail("datum-plane PAD orientation is incorrect");
    }
    return 0;
}
