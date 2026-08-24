#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/model.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/compiler/dependency_graph.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {

constexpr std::string_view plate = R"ICAD(REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
PROJECT plate
UNITS mm
PARAMETER thickness 8 mm
PARAMETER radius 5 mm
BODY mounting_plate
SKETCH layout ON PLANE XY
SHAPE outer CLOSED ROLE STOCK
POINT p0 0 mm 0 mm FIXED
POINT p1 100 mm 0 mm FIXED
POINT p2 100 mm 60 mm FIXED
POINT p3 0 mm 60 mm FIXED
LINE bottom FROM p0 TO p1
LINE right FROM p1 TO p2
LINE top FROM p2 TO p3
LINE left FROM p3 TO p0
END
SHAPE hole_0 CLOSED ROLE HOLE
POINT center 15 mm 15 mm FIXED
CIRCLE rim CENTER center RADIUS radius
END
SHAPE hole_1 CLOSED ROLE HOLE
POINT center 85 mm 45 mm FIXED
CIRCLE rim CENTER center RADIUS radius
END
SOLVE FULL
END
PAD stock FROM layout.outer DEPTH thickness NEW
POCKET bore_0 FROM layout.hole_0 DEPTH thickness
POCKET bore_1 FROM layout.hole_1 DEPTH thickness
END
)ICAD";

constexpr std::string_view region_plate = R"ICAD(REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
REQUIRES CAPABILITY SKETCH_REGION_ARRANGEMENT_V1
PROJECT region_plate
UNITS mm
BODY mounting_plate
SKETCH layout ON PLANE XY
SHAPE outer CLOSED ROLE STOCK
POINT p0 0 mm 0 mm FIXED
POINT p1 100 mm 0 mm FIXED
POINT p2 100 mm 60 mm FIXED
POINT p3 0 mm 60 mm FIXED
LINE bottom FROM p0 TO p1
LINE right FROM p1 TO p2
LINE top FROM p2 TO p3
LINE left FROM p3 TO p0
END
SHAPE hole_0 CLOSED ROLE HOLE
POINT center 15 mm 15 mm FIXED
CIRCLE rim CENTER center RADIUS 5 mm
END
SHAPE hole_1 CLOSED ROLE HOLE
POINT center 85 mm 45 mm FIXED
CIRCLE rim CENTER center RADIUS 5 mm
END
REGION perforated_plate
OUTER outer
HOLES hole_0 hole_1
END
SOLVE FULL
END
PAD solid FROM layout.perforated_plate DEPTH 8 mm NEW
END
)ICAD";

constexpr std::string_view advanced_constraints = R"ICAD(REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
REQUIRES CAPABILITY ADVANCED_SKETCH_CONSTRAINTS_V1
PROJECT advanced_constraints
UNITS mm
BODY specimen
SKETCH layout ON PLANE XY
SHAPE frame CLOSED ROLE STOCK
POINT p0 0 mm 0 mm FIXED
POINT p1 100 mm 0 mm FIXED
POINT p2 100 mm 60 mm FIXED
POINT p3 0 mm 60 mm FIXED
LINE bottom FROM p0 TO p1
LINE right FROM p1 TO p2
LINE top FROM p2 TO p3
LINE left FROM p3 TO p0
END
SHAPE circle_a OPEN ROLE CONSTRUCTION
POINT center 30 mm 30 mm FIXED
CIRCLE rim CENTER center RADIUS 5 mm
END
SHAPE circle_b OPEN ROLE CONSTRUCTION
POINT center 30 mm 30 mm FIXED
CIRCLE rim CENTER center RADIUS 5 mm
END
SHAPE guide OPEN ROLE CONSTRUCTION
POINT left 40 mm 30 mm FIXED
POINT right 60 mm 30 mm FIXED
POINT below 50 mm 20 mm FIXED
POINT above 50 mm 40 mm FIXED
POINT middle 50 mm 30 mm FIXED
LINE horizontal FROM left TO right
LINE vertical FROM below TO above
END
CONSTRAINT width H_DISTANCE frame.p0 frame.p1 100 mm
CONSTRAINT height V_DISTANCE frame.p1 frame.p2 60 mm
CONSTRAINT parallel PARALLEL frame.bottom frame.top
CONSTRAINT perpendicular PERPENDICULAR frame.bottom frame.right
CONSTRAINT equal_length EQUAL_LENGTH frame.bottom frame.top
CONSTRAINT concentric CONCENTRIC circle_a.rim circle_b.rim
CONSTRAINT equal_radius EQUAL_RADIUS circle_a.rim circle_b.rim
CONSTRAINT midpoint MIDPOINT guide.middle guide.horizontal
CONSTRAINT symmetry SYMMETRIC guide.left guide.right ABOUT guide.vertical
SOLVE FULL
END
PAD solid FROM layout.frame DEPTH 2 mm NEW
END
)ICAD";

constexpr std::string_view tangent_profile = R"ICAD(REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
REQUIRES CAPABILITY SKETCH_LINE_ARC_TANGENCY_V1
PROJECT tangent_profile
UNITS mm
BODY plate
SKETCH outline ON PLANE XY
SHAPE capsule CLOSED ROLE STOCK
POINT p0 -30 mm -10 mm FIXED
POINT p1 30 mm -10 mm FIXED
POINT p2 30 mm 10 mm FIXED
POINT p3 -30 mm 10 mm FIXED
POINT c1 30 mm 0 mm FIXED
POINT c0 -30 mm 0 mm FIXED
LINE bottom FROM p0 TO p1
ARC right FROM p1 TO p2 CENTER c1 CCW
LINE top FROM p2 TO p3
ARC left FROM p3 TO p0 CENTER c0 CCW
END
CONSTRAINT t0 TANGENT capsule.bottom capsule.right AT capsule.p1
CONSTRAINT t1 TANGENT capsule.right capsule.top AT capsule.p2
CONSTRAINT t2 TANGENT capsule.top capsule.left AT capsule.p3
CONSTRAINT t3 TANGENT capsule.left capsule.bottom AT capsule.p0
SOLVE FULL
END
PAD solid FROM outline.capsule DEPTH 8 mm NEW
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result,
                            std::string_view code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) {
                                   return diagnostic.code == code;
                               });
}

} // namespace

auto main() -> int {
    const auto result = icad::compiler::compile(plate);
    if (!result.ok()) {
        for (const auto& diagnostic : result.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return fail("multi-shape plate did not compile");
    }
    if (result.ir_project->sketches.size() != 1 ||
        result.ir_project->sketches.front().shapes.size() != 3 ||
        result.ir_project->profiles.size() != 3 ||
        result.ir_project->bodies.front().features.size() != 3) {
        return fail("multi-shape sketch did not lower into independent profiles and features");
    }
    const auto& sketch = result.ir_project->sketches.front();
    if (sketch.status != icad::compiler::ir::SketchSolveStatus::fully_constrained ||
        sketch.degrees_of_freedom != 0 || sketch.shapes[0].role != "STOCK" ||
        sketch.shapes[1].role != "HOLE" || sketch.shapes[1].containing_shape != "outer" ||
        sketch.shapes[1].profile != "mounting_plate::layout.hole_0" ||
        sketch.shapes[1].area_mm2 < 78.0 || sketch.shapes[1].area_mm2 > 79.0) {
        return fail("shape classification or solved region evidence is incorrect");
    }
    const auto model = icad::cad::build_model(*result.ir_project);
    if (!icad::cad::is_valid(model))
        return fail("multi-shape history did not produce valid native geometry");
    if (model.parts.size() != 1 ||
        std::ranges::none_of(model.parts.front().repairs, [](const auto& repair) {
            return repair.contains("batched 2 compatible cut features");
        })) {
        return fail("compatible pocket cuts were not evaluated in one boolean transaction");
    }
    const auto measurements = icad::cad::analyze(*result.ir_project);
    const double expected_volume = 100.0 * 60.0 * 8.0 - 2.0 * std::numbers::pi * 25.0 * 8.0;
    if (measurements.volume_mm3 < expected_volume * 0.97 ||
        measurements.volume_mm3 > expected_volume * 1.03) {
        return fail("multi-shape pockets produced an unexpected volume");
    }
    const auto visual = icad::ai::visual_snapshot_json(*result.ir_project);
    if (!visual.contains("\"name\":\"outer\",\"role\":\"STOCK\"") ||
        !visual.contains("\"name\":\"hole_0\",\"role\":\"HOLE\"") ||
        !visual.contains("\"containedBy\":\"outer\"") ||
        !visual.contains("\"type\":\"CIRCLE\"") ||
        !visual.contains("\"solveRequirement\":\"FULL\"")) {
        return fail("visual.json omitted multi-shape region evidence");
    }
    const auto dependencies = icad::compiler::build_dependency_graph(*result.ir_project);
    const auto profile = std::ranges::find(dependencies.nodes,
                                           "profile:mounting_plate::layout.hole_0",
                                           &icad::compiler::DependencyNode::id);
    if (profile == dependencies.nodes.end() ||
        !std::ranges::contains(profile->dependencies,
                               "sketch:mounting_plate::layout")) {
        return fail("shape profile is detached from its owning sketch dependency");
    }

    const auto region_result = icad::compiler::compile(region_plate);
    if (!region_result.ok()) {
        for (const auto& diagnostic : region_result.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return fail("explicit sketch REGION did not compile");
    }
    const auto& region_sketch = region_result.ir_project->sketches.front();
    const auto& region = region_sketch.regions.front();
    const auto& region_feature = region_result.ir_project->bodies.front().features.front();
    if (region_sketch.regions.size() != 1 || region.hole_profiles.size() != 2 ||
        region.area_mm2 < 5842.0 || region.area_mm2 > 5844.0 ||
        region_feature.region != "mounting_plate::layout.perforated_plate" ||
        region_feature.profile != "mounting_plate::layout.outer" ||
        region_feature.region_hole_profiles.size() != 2) {
        return fail("REGION did not lower to one outer profile plus classified holes");
    }
    const auto region_model = icad::cad::build_model(*region_result.ir_project);
    if (!icad::cad::is_valid(region_model) || region_model.parts.size() != 1 ||
        std::ranges::none_of(region_model.parts.front().repairs, [](const auto& repair) {
            return repair.contains("REGION hole profile");
        })) {
        return fail("REGION did not generate one valid native solid");
    }
    const auto region_measurements = icad::cad::analyze(*region_result.ir_project);
    const double expected_region_volume =
        100.0 * 60.0 * 8.0 - 2.0 * std::numbers::pi * 25.0 * 8.0;
    if (region_measurements.volume_mm3 < expected_region_volume * 0.97 ||
        region_measurements.volume_mm3 > expected_region_volume * 1.03) {
        return fail("REGION solid produced an unexpected volume");
    }
    const auto region_visual = icad::ai::visual_snapshot_json(*region_result.ir_project);
    if (!region_visual.contains("\"name\":\"perforated_plate\"") ||
        !region_visual.contains("\"outer\":\"outer\"") ||
        !region_visual.contains("\"regionHoleProfiles\":2")) {
        return fail("visual.json omitted explicit REGION evidence");
    }
    const auto region_dependencies =
        icad::compiler::build_dependency_graph(*region_result.ir_project);
    const auto region_node = std::ranges::find(
        region_dependencies.nodes, "region:mounting_plate::layout.perforated_plate",
        &icad::compiler::DependencyNode::id);
    if (region_node == region_dependencies.nodes.end() ||
        region_node->dependencies.size() != 3) {
        return fail("REGION dependency node did not retain outer and hole profiles");
    }

    const auto advanced = icad::compiler::compile(advanced_constraints);
    if (!advanced.ok()) {
        for (const auto& diagnostic : advanced.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return fail("advanced industrial sketch constraints did not compile");
    }
    const auto& advanced_sketch = advanced.ir_project->sketches.front();
    if (advanced_sketch.constraints.size() != 9 ||
        advanced_sketch.status != icad::compiler::ir::SketchSolveStatus::fully_constrained ||
        advanced_sketch.maximum_residual > 1e-6) {
        return fail("advanced constraint family was not solved deterministically");
    }

    const auto tangent = icad::compiler::compile(tangent_profile);
    if (!tangent.ok()) {
        for (const auto& diagnostic : tangent.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        return fail("line-arc tangent profile did not compile");
    }
    const auto& tangent_sketch = tangent.ir_project->sketches.front();
    if (tangent_sketch.constraints.size() != 4 ||
        tangent_sketch.status != icad::compiler::ir::SketchSolveStatus::fully_constrained ||
        tangent_sketch.maximum_residual > 1e-8) {
        return fail("line-arc endpoint tangencies were not solved");
    }
    const auto tangent_visual = icad::ai::visual_snapshot_json(*tangent.ir_project);
    if (!tangent_visual.contains("\"type\":\"TANGENT\"") ||
        !tangent_visual.contains("\"capsule.right\"") ||
        !tangent_visual.contains("\"capsule.p1\"")) {
        return fail("visual.json omitted tangent constraint evidence");
    }

    const auto invalid_tangent_type = icad::compiler::compile(
        "REQUIRES CAPABILITY SKETCH_LINE_ARC_TANGENCY_V1\n"
        "PROJECT bad_tangent_type\nUNITS mm\nBODY part\n"
        "SKETCH outline ON PLANE XY\nSHAPE shape CLOSED ROLE STOCK\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 10 mm 0 mm FIXED\n"
        "POINT p2 10 mm 10 mm FIXED\nPOINT p3 0 mm 10 mm FIXED\n"
        "LINE e0 FROM p0 TO p1\nLINE e1 FROM p1 TO p2\n"
        "LINE e2 FROM p2 TO p3\nLINE e3 FROM p3 TO p0\nEND\n"
        "CONSTRAINT tangent TANGENT shape.e0 shape.e1 AT shape.p1\nEND\n"
        "PAD solid FROM outline.shape DEPTH 2 mm NEW\nEND\n");
    const auto invalid_tangent_contact = icad::compiler::compile(
        "REQUIRES CAPABILITY SKETCH_LINE_ARC_TANGENCY_V1\n"
        "PROJECT bad_tangent_contact\nUNITS mm\nBODY part\n"
        "SKETCH outline ON PLANE XY\nSHAPE shape CLOSED ROLE STOCK\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 10 mm 0 mm FIXED\n"
        "POINT p2 10 mm 10 mm FIXED\nPOINT p3 0 mm 10 mm FIXED\n"
        "POINT c 5 mm 10 mm FIXED\nLINE bottom FROM p0 TO p1\n"
        "LINE right FROM p1 TO p2\nARC top FROM p2 TO p3 CENTER c CCW\n"
        "LINE left FROM p3 TO p0\nEND\n"
        "CONSTRAINT tangent TANGENT shape.bottom shape.top AT shape.p0\nEND\n"
        "PAD solid FROM outline.shape DEPTH 2 mm NEW\nEND\n");
    const auto inconsistent_tangent = icad::compiler::compile(
        "REQUIRES CAPABILITY SKETCH_LINE_ARC_TANGENCY_V1\n"
        "PROJECT inconsistent_tangent\nUNITS mm\nBODY part\n"
        "SKETCH outline ON PLANE XY\nSHAPE shape CLOSED ROLE STOCK\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 10 mm 0 mm FIXED\n"
        "POINT p2 10 mm 10 mm FIXED\nPOINT p3 0 mm 10 mm FIXED\n"
        "POINT c 5 mm 5 mm FIXED\nLINE bottom FROM p0 TO p1\n"
        "ARC end FROM p1 TO p2 CENTER c CCW\nLINE top FROM p2 TO p3\n"
        "LINE left FROM p3 TO p0\nEND\n"
        "CONSTRAINT tangent TANGENT shape.bottom shape.end AT shape.p1\nEND\n"
        "PAD solid FROM outline.shape DEPTH 2 mm NEW\nEND\n");
    if (invalid_tangent_type.ok() || !has_code(invalid_tangent_type, "ICAD-S0042") ||
        invalid_tangent_contact.ok() || !has_code(invalid_tangent_contact, "ICAD-S0042") ||
        inconsistent_tangent.ok() || !has_code(inconsistent_tangent, "ICAD-S0044")) {
        return fail("invalid or inconsistent TANGENT constraints were accepted");
    }

    const auto open_stock = icad::compiler::compile(
        "PROJECT bad_open\nUNITS mm\nBODY part\nSKETCH layout ON PLANE XY\n"
        "SHAPE outer OPEN ROLE STOCK\nPOINT p0 0 mm 0 mm FIXED\n"
        "POINT p1 10 mm 0 mm FIXED\nLINE edge FROM p0 TO p1\nEND\nEND\n"
        "PAD solid FROM layout.outer DEPTH 2 mm NEW\nEND\n");
    if (open_stock.ok() || !has_code(open_stock, "ICAD-S0042"))
        return fail("OPEN STOCK shape was not rejected");

    const auto self_crossing = icad::compiler::compile(
        "PROJECT bad_cross\nUNITS mm\nBODY part\nSKETCH layout ON PLANE XY\n"
        "SHAPE outer CLOSED ROLE STOCK\nPOINT p0 0 mm 0 mm FIXED\n"
        "POINT p1 10 mm 10 mm FIXED\nPOINT p2 0 mm 10 mm FIXED\n"
        "POINT p3 10 mm 0 mm FIXED\nLINE e0 FROM p0 TO p1\nLINE e1 FROM p1 TO p2\n"
        "LINE e2 FROM p2 TO p3\nLINE e3 FROM p3 TO p0\nEND\nEND\n"
        "PAD solid FROM layout.outer DEPTH 2 mm NEW\nEND\n");
    if (self_crossing.ok() || !has_code(self_crossing, "ICAD-S0043"))
        return fail("self-intersecting SHAPE was not rejected");

    const auto outside_hole = icad::compiler::compile(
        "PROJECT bad_hole\nUNITS mm\nBODY part\nSKETCH layout ON PLANE XY\n"
        "SHAPE outer CLOSED ROLE STOCK\nPOINT p0 0 mm 0 mm FIXED\n"
        "POINT p1 10 mm 0 mm FIXED\nPOINT p2 10 mm 10 mm FIXED\n"
        "POINT p3 0 mm 10 mm FIXED\nLINE e0 FROM p0 TO p1\nLINE e1 FROM p1 TO p2\n"
        "LINE e2 FROM p2 TO p3\nLINE e3 FROM p3 TO p0\nEND\n"
        "SHAPE hole CLOSED ROLE HOLE\nPOINT center 20 mm 20 mm FIXED\n"
        "CIRCLE rim CENTER center RADIUS 1 mm\nEND\nEND\n"
        "PAD solid FROM layout.outer DEPTH 2 mm NEW\n"
        "POCKET bore FROM layout.hole DEPTH 2 mm\nEND\n");
    if (outside_hole.ok() || !has_code(outside_hole, "ICAD-S0045"))
        return fail("uncontained HOLE shape was not rejected");

    const auto invalid_region = icad::compiler::compile(
        "PROJECT bad_region\nUNITS mm\nBODY part\nSKETCH layout ON PLANE XY\n"
        "SHAPE outer CLOSED ROLE STOCK\nPOINT p0 0 mm 0 mm FIXED\n"
        "POINT p1 20 mm 0 mm FIXED\nPOINT p2 20 mm 20 mm FIXED\n"
        "POINT p3 0 mm 20 mm FIXED\nLINE e0 FROM p0 TO p1\nLINE e1 FROM p1 TO p2\n"
        "LINE e2 FROM p2 TO p3\nLINE e3 FROM p3 TO p0\nEND\n"
        "REGION invalid\nOUTER outer\nHOLES outer\nEND\nEND\n"
        "PAD solid FROM layout.invalid DEPTH 2 mm NEW\nEND\n");
    if (invalid_region.ok() || !has_code(invalid_region, "ICAD-P0029"))
        return fail("REGION did not reject an OUTER reused as a HOLE");

    const auto cross_shape_constraints = icad::compiler::compile(
        "PROJECT constrained_regions\nUNITS mm\nBODY part\n"
        "SKETCH layout ON PLANE XY\nSHAPE outer CLOSED ROLE STOCK\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT px 20 mm 0 mm FIXED\n"
        "POINT py 0 mm 20 mm FIXED\nPOINT p1 40 mm 0 mm FIXED\n"
        "POINT p2 40 mm 40 mm FIXED\nPOINT p3 0 mm 40 mm FIXED\n"
        "LINE e0 FROM p0 TO p1\nLINE e1 FROM p1 TO p2\n"
        "LINE e2 FROM p2 TO p3\nLINE e3 FROM p3 TO p0\nEND\n"
        "SHAPE hole CLOSED ROLE HOLE\nPOINT center 18 mm 18 mm\n"
        "CIRCLE rim CENTER center RADIUS 3 mm\nEND\n"
        "CONSTRAINT center_x VERTICAL outer.px hole.center\n"
        "CONSTRAINT center_y HORIZONTAL outer.py hole.center\nSOLVE FULL\nEND\n"
        "PAD solid FROM layout.outer DEPTH 2 mm NEW\n"
        "POCKET bore FROM layout.hole DEPTH 2 mm\nEND\n");
    if (!cross_shape_constraints.ok() ||
        cross_shape_constraints.ir_project->sketches.front().degrees_of_freedom != 0) {
        return fail("qualified cross-shape constraints did not fully solve the workspace");
    }

    const auto underconstrained_legacy = icad::compiler::compile(
        "PROJECT under\nUNITS mm\nSKETCH outline\n"
        "POINT p0 0 mm 0 mm FIXED\nPOINT p1 10 mm 0 mm\n"
        "POINT p2 0 mm 10 mm\nLINE e0 FROM p0 TO p1\n"
        "LINE e1 FROM p1 TO p2\nLINE e2 FROM p2 TO p0\n"
        "SOLVE FULL\nEND\n");
    if (underconstrained_legacy.ok() || !has_code(underconstrained_legacy, "ICAD-S0044"))
        return fail("SOLVE FULL did not reject an under-constrained legacy sketch");

    return 0;
}
