#include "icad/ai/inspector.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/compiler/dependency_graph.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view valid_source = R"ICAD(
REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
REQUIRES CAPABILITY SKETCH_REGION_ARRANGEMENT_V1
REQUIRES CAPABILITY TOPOLOGY_QUERY_V1
PROJECT topology_query
UNITS mm
BODY vessel
SKETCH annulus ON PLANE XY
SHAPE outside CLOSED ROLE STOCK
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS 40 mm
END
SHAPE inside CLOSED ROLE HOLE
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS 32 mm
END
REGION wall
OUTER outside
HOLES inside
END
SOLVE FULL
END
PAD wall_solid FROM annulus.wall DEPTH 50 mm NEW
SELECTION upper_inner_rim
FROM wall_solid
EDGES WHERE
LOOP
CIRCULAR
CONCAVE
ADJACENT_TO FACE top
END
FEATURE soften_rim
TYPE FILLET
SELECT EDGESET upper_inner_rim
RADIUS 3 mm
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
    const auto result = icad::compiler::compile(valid_source);
    if (!result.ok() || !result.ir_project || !result.topology_model)
        return fail("named topology-query fixture did not compile");
    if (!icad::cad::validate_topology(*result.topology_model).valid())
        return fail("named topology query produced invalid native topology");

    const auto& body = result.ir_project->bodies.front();
    if (body.topology_selections.size() != 1 || body.features.size() != 2)
        return fail("topology query did not lower beside its feature history");
    const auto& selection = body.topology_selections.front();
    const auto& modifier = body.features.back();
    if (selection.name != "upper_inner_rim" || selection.source_feature != "wall_solid" ||
        selection.entity_kind != "EDGE_LOOP" || selection.geometry != "CIRCULAR" ||
        selection.convexity != "CONCAVE" || selection.adjacent_face != "TOP" ||
        selection.topology_id != "vessel/wall_solid/edge.loop.top.inner" ||
        modifier.selected_edge_set != "upper_inner_rim" ||
        modifier.selected_edge_location != "TOP" ||
        modifier.selected_edge_classification != "INNER" ||
        modifier.selected_topology_id != selection.topology_id) {
        return fail("typed topology query or resolved modifier contract is incomplete");
    }

    const auto dependencies = icad::compiler::build_dependency_graph(*result.ir_project);
    const auto selection_node = std::ranges::find(
        dependencies.nodes, std::string{"selection:vessel/upper_inner_rim"},
        &icad::compiler::DependencyNode::id);
    if (selection_node == dependencies.nodes.end() ||
        !std::ranges::contains(selection_node->dependencies,
                               std::string{"feature:vessel/wall_solid"})) {
        return fail("topology selection is absent from the dependency graph");
    }

    const auto visual = icad::ai::visual_snapshot_json(*result.ir_project);
    if (!visual.contains("\"matchedTopologyId\":\"vessel/wall_solid/edge.loop.top.inner\"") ||
        !visual.contains("\"reference\":\"upper_inner_rim\"") ||
        !visual.contains("matched one circular concave edge loop adjacent to the top face") ||
        !visual.contains("\"allowed\":[\"FILLET\",\"CHAMFER\"]") ||
        !visual.contains("\"operation\":\"SHELL\",\"reason\":")) {
        return fail("visual contract omitted query evidence or operation applicability");
    }

    const auto missing_predicate = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY vessel\nSELECTION rim\nFROM wall\n"
        "EDGES WHERE\nLOOP\nCIRCULAR\nCONCAVE\nEND\nEND\n");
    if (missing_predicate.ok() || !has_code(missing_predicate, "ICAD-P0031"))
        return fail("parser accepted a topology query without an adjacent-face predicate");

    const auto unknown_selection = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY b\nFEATURE stock\nTYPE BOX\nWIDTH 4 mm\n"
        "DEPTH 4 mm\nHEIGHT 4 mm\nEND\nFEATURE round\nTYPE FILLET\n"
        "SELECT EDGESET missing\nRADIUS 1 mm\nEND\nEND\n");
    if (unknown_selection.ok() || !has_code(unknown_selection, "ICAD-S0047"))
        return fail("semantic analysis accepted an unknown named topology selection");

    const auto non_annular_source = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY b\nSKETCH disk ON PLANE XY\n"
        "CIRCLE 0 mm 0 mm 10 mm\nEND\nPAD solid FROM disk DEPTH 5 mm NEW\n"
        "SELECTION rim\nFROM solid\nEDGES WHERE\nLOOP\nCIRCULAR\nCONVEX\n"
        "ADJACENT_TO FACE top\nEND\nFEATURE round\nTYPE FILLET\n"
        "SELECT EDGESET rim\nRADIUS 1 mm\nEND\nEND\n");
    if (non_annular_source.ok() || !has_code(non_annular_source, "ICAD-S0047"))
        return fail("semantic analysis accepted a query on an unsupported source topology");

    const auto stale_source = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY b\nSKETCH annulus ON PLANE XY\n"
        "SHAPE outside CLOSED ROLE STOCK\nPOINT center 0 mm 0 mm FIXED\n"
        "CIRCLE rim CENTER center RADIUS 10 mm\nEND\n"
        "SHAPE inside CLOSED ROLE HOLE\nPOINT center 0 mm 0 mm FIXED\n"
        "CIRCLE rim CENTER center RADIUS 8 mm\nEND\nREGION wall\nOUTER outside\n"
        "HOLES inside\nEND\nSOLVE FULL\nEND\nPAD solid FROM annulus.wall DEPTH 5 mm NEW\n"
        "SELECTION rim\nFROM solid\nEDGES WHERE\nLOOP\nCIRCULAR\nCONCAVE\n"
        "ADJACENT_TO FACE top\nEND\nFEATURE translate\nTYPE TRANSLATE\n"
        "DIRECTION x\nDISTANCE 1 mm\nEND\nFEATURE round\nTYPE FILLET\n"
        "SELECT EDGESET rim\nRADIUS 1 mm\nEND\nEND\n");
    if (stale_source.ok() || !has_code(stale_source, "ICAD-S0047"))
        return fail("semantic analysis silently remapped a selection through later history");

    const auto wrong_operation = icad::compiler::compile(
        "PROJECT bad\nUNITS mm\nBODY b\nFEATURE stock\nTYPE BOX\nWIDTH 4 mm\n"
        "DEPTH 4 mm\nHEIGHT 4 mm\nEND\nFEATURE extra\nTYPE BOX\n"
        "SELECT EDGESET any\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\nEND\nEND\n");
    if (wrong_operation.ok() || !has_code(wrong_operation, "ICAD-S0035"))
        return fail("non-edge modifier accepted a named edge selection");

    return 0;
}
