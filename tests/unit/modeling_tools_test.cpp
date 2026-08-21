#include "icad/ai/inspector.hpp"
#include "icad/cad/analysis.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT ModelingTools
UNITS mm
POINT3 chamfer_edge 20 mm 10 mm 5 mm
POINT3 fillet_edge 40 mm 0 mm 0 mm
POINT3 mirror_plane 65 mm 0 mm 0 mm
VECTOR x_axis 1 0 0
BODY chamfered
FEATURE stock
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 10 mm
END
FEATURE bevel
TYPE CHAMFER
SELECT EDGE NEAREST chamfer_edge
DISTANCE 2 mm
END
END
BODY filleted
FEATURE stock
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 10 mm
ORIGIN_X 30 mm
END
FEATURE round
TYPE FILLET
SELECT EDGE NEAREST fillet_edge
RADIUS 2 mm
END
END
BODY patterned
FEATURE seed
TYPE BOX
WIDTH 5 mm
DEPTH 5 mm
HEIGHT 5 mm
END
FEATURE row
TYPE LINEAR_PATTERN
DIRECTION x_axis
COUNT 3
SPACING 10 mm
END
END
BODY mirrored
FEATURE seed
TYPE BOX
WIDTH 5 mm
DEPTH 5 mm
HEIGHT 5 mm
ORIGIN_X 70 mm
END
FEATURE pair
TYPE MIRROR
PLANE mirror_plane NORMAL x_axis
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
        return fail("modeling tool fixture did not compile");
    const auto validation = icad::cad::validate_topology(*compilation.topology_model);
    if (!validation.valid() || compilation.topology_model->solids.size() != 7)
        return fail("modeling tools did not emit seven validated solids");

    const auto metrics = icad::cad::analyze(*compilation.ir_project);
    if (std::abs(metrics.volume_mm3 - 4587.42890304516) > 1e-7 || metrics.parts.size() != 7)
        return fail("modeling tools produced incorrect deterministic volume");

    const auto inspection = icad::ai::project_json(*compilation.ir_project);
    if (!inspection.contains("\"modelingOperations\":4") ||
        !inspection.contains("\"edgeNearestPoint\":\"fillet_edge\"") ||
        !inspection.contains("\"planeNormal\":\"x_axis\""))
        return fail("agent inspection omitted semantic modeling operations");
    for (const std::string_view expected : {"native CHAMFER", "native FILLET",
                                            "linear-pattern instances", "mirrored solid"}) {
        if (!inspection.contains(expected))
            return fail("agent inspection omitted modeling provenance");
    }
    const auto first_modifier = icad::compiler::compile(
        "PROJECT invalid\nUNITS mm\nPOINT3 p 0 mm 0 mm 0 mm\nBODY b\nFEATURE f\n"
        "TYPE CHAMFER\nSELECT EDGE NEAREST p\nDISTANCE 1 mm\nEND\nEND\n");
    if (first_modifier.ok() || !has_code(first_modifier, "ICAD-S0035"))
        return fail("semantic analysis accepted a modifier without a source solid");

    const auto unknown_selector = icad::compiler::compile(
        "PROJECT invalid\nUNITS mm\nBODY b\nFEATURE stock\nTYPE BOX\nWIDTH 2 mm\n"
        "DEPTH 2 mm\nHEIGHT 2 mm\nEND\nFEATURE f\nTYPE FILLET\n"
        "SELECT EDGE NEAREST missing\nRADIUS 1 mm\nEND\nEND\n");
    if (unknown_selector.ok() || !has_code(unknown_selector, "ICAD-S0035"))
        return fail("semantic analysis accepted an unknown edge selector point");
    return 0;
}
