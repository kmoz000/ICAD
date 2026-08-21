#include "icad/ai/inspector.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view source = R"ICAD(
PROJECT TopologyTest
UNITS mm
PROFILE polygon
POINT 4 mm 0 mm
POINT 8 mm 0 mm
POINT 8 mm 5 mm
POINT 4 mm 5 mm
END
BODY exact
FEATURE box
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
ROTATION_X 15 deg
END
FEATURE cylinder
TYPE CYLINDER
RADIUS 4 mm
HEIGHT 12 mm
ROTATION_Y 25 deg
END
FEATURE cone
TYPE CONE
RADIUS1 5 mm
RADIUS2 2 mm
HEIGHT 9 mm
END
FEATURE sphere
TYPE SPHERE
RADIUS 6 mm
ORIGIN_X 30 mm
END
FEATURE prism
TYPE EXTRUDE
PROFILE polygon
HEIGHT 7 mm
END
FEATURE ring
TYPE REVOLVE
PROFILE polygon
ANGLE 360 deg
END
END
)ICAD";

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto close_to_one(const icad::cad::Vector3& vector) -> bool {
    const double length =
        std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    return std::abs(length - 1.0) <= 1e-9;
}

} // namespace

auto main() -> int {
    const auto compiled = icad::compiler::compile(source);
    if (!compiled.ok() || !compiled.topology_model) {
        return fail("compiler did not produce exact topology");
    }
    const auto& topology = *compiled.topology_model;
    const auto validation = icad::cad::validate_topology(topology);
    if (!validation.valid()) {
        return fail("compiler-produced topology is invalid");
    }
    if (topology.solids.size() != 6 || topology.vertex_count() != 26 ||
        topology.edge_count() != 40 || topology.wire_count() != 24 || topology.face_count() != 24) {
        return fail("exact topology entity counts changed unexpectedly");
    }
    const auto revolved =
        std::ranges::find(topology.solids, "ring", &icad::cad::SolidTopology::feature);
    if (revolved == topology.solids.end() || revolved->euler_characteristic() != 0) {
        return fail("full revolve did not preserve genus-one topology");
    }
    for (const auto& solid : topology.solids) {
        for (const auto& edge : solid.edges) {
            if (!close_to_one(edge.curve.direction) || !close_to_one(edge.curve.axis)) {
                return fail("transformed analytic curve frame is not normalized");
            }
        }
        for (const auto& face : solid.faces) {
            if (!close_to_one(face.surface.axis)) {
                return fail("transformed analytic surface axis is not normalized");
            }
        }
    }

    std::string resized{source};
    resized.replace(resized.find("WIDTH 20 mm"), std::string_view{"WIDTH 20 mm"}.size(),
                    "WIDTH 35 mm");
    const auto resized_compilation = icad::compiler::compile(resized);
    if (!resized_compilation.ok() ||
        resized_compilation.topology_model->solids.front().faces.front().id !=
            topology.solids.front().faces.front().id ||
        resized_compilation.topology_model->solids.front().edges.front().id !=
            topology.solids.front().edges.front().id) {
        return fail("semantic topology IDs changed after a dimension edit");
    }

    auto corrupted = topology;
    corrupted.solids.front().edges.front().end_vertex = "missing/vertex";
    const auto rejected = icad::cad::validate_topology(corrupted);
    if (rejected.valid() || std::ranges::none_of(rejected.issues, [](const auto& issue) {
            return issue.code == "ICAD-G0007";
        })) {
        return fail("topology validator accepted a missing vertex reference");
    }

    auto detached_surface = topology;
    detached_surface.solids.front().faces.front().surface.origin.z += 1.0;
    const auto detached_rejection = icad::cad::validate_topology(detached_surface);
    if (detached_rejection.valid() ||
        std::ranges::none_of(detached_rejection.issues,
                             [](const auto& issue) { return issue.code == "ICAD-G0024"; })) {
        return fail("topology validator accepted a boundary detached from its surface");
    }

    const auto json = icad::ai::topology_json(*compiled.ir_project);
    if (!json.contains("\"schema\":\"icad.topology.v1\"") ||
        !json.contains("exact/ring/face.revolved.0") || !json.contains("\"surface\":\"cone\"") ||
        !json.contains("\"genus\":1")) {
        return fail("agent topology JSON omitted stable analytic entities");
    }
    return 0;
}
