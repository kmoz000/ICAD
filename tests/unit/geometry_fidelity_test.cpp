#include "icad/cad/analysis.hpp"
#include "icad/cad/model.hpp"
#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <string_view>

namespace {

constexpr double tolerance = 1.0e-6;

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

[[nodiscard]] auto close(double first, double second, double epsilon = tolerance) -> bool {
    return std::abs(first - second) <= epsilon;
}

[[nodiscard]] auto angular_distance(double first, double second) -> double {
    double distance = std::abs(first - second);
    return std::min(distance, 2.0 * std::numbers::pi - distance);
}

constexpr std::string_view box_source = R"ICAD(
PROJECT known_box
UNITS mm
BODY block
FEATURE stock
TYPE BOX
WIDTH 20 mm
DEPTH 10 mm
HEIGHT 5 mm
END
END
)ICAD";

constexpr std::string_view vessel_source = R"ICAD(
REQUIRES ICAD 1.0
REQUIRES CAPABILITY MULTI_SHAPE_SKETCH_V1
REQUIRES CAPABILITY SKETCH_REGION_ARRANGEMENT_V1
REQUIRES CAPABILITY SEMANTIC_EDGE_LOOP_SELECTION_V1
PROJECT known_filleted_vessels
UNITS mm
PARAMETER outer_radius 40 mm
PARAMETER inner_radius 32 mm
PARAMETER height 50 mm
PARAMETER edge_round 3 mm
POINT3 comparison_origin 100 mm 0 mm 0 mm

BODY inner_finish
SKETCH annulus ON PLANE XY
SHAPE outside CLOSED ROLE STOCK
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS outer_radius
END
SHAPE inside CLOSED ROLE HOLE
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS inner_radius
END
REGION wall
OUTER outside
HOLES inside
END
SOLVE FULL
END
PAD wall_solid FROM annulus.wall DEPTH height NEW
FEATURE finish
TYPE FILLET
SELECT EDGE TOP INNER
RADIUS edge_round
END
END

BODY outer_finish
SKETCH annulus ON PLANE XY
SHAPE outside CLOSED ROLE STOCK
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS outer_radius
END
SHAPE inside CLOSED ROLE HOLE
POINT center 0 mm 0 mm FIXED
CIRCLE rim CENTER center RADIUS inner_radius
END
REGION wall
OUTER outside
HOLES inside
END
SOLVE FULL
END
PAD wall_solid FROM annulus.wall DEPTH height NEW
FEATURE finish
TYPE FILLET
SELECT EDGE TOP OUTER
RADIUS edge_round
END
END
POSE outer_finish AT comparison_origin ROTATION 0 deg 0 deg 0 deg
)ICAD";

[[nodiscard]] auto verify_box() -> bool {
    const auto compilation = icad::compiler::compile(box_source);
    if (!compilation.ok())
        return false;
    const auto model = icad::cad::build_model(*compilation.ir_project);
    if (!icad::cad::is_valid(model) || model.parts.size() != 1 ||
        model.parts.front().vertices.size() != 8 || model.parts.front().triangles.size() != 12) {
        return false;
    }
    const auto analysis = icad::cad::analyze(*compilation.ir_project);
    return analysis.parts.size() == 1 && close(analysis.bounds.minimum[0], 0.0) &&
           close(analysis.bounds.minimum[1], 0.0) && close(analysis.bounds.minimum[2], 0.0) &&
           close(analysis.bounds.maximum[0], 20.0) && close(analysis.bounds.maximum[1], 10.0) &&
           close(analysis.bounds.maximum[2], 5.0) && close(analysis.volume_mm3, 1000.0) &&
           close(analysis.surface_area_mm2, 700.0);
}

[[nodiscard]] auto verify_vessels() -> bool {
    const auto compilation = icad::compiler::compile(vessel_source);
    if (!compilation.ok())
        return false;
    const auto model = icad::cad::build_model(*compilation.ir_project);
    if (!icad::cad::is_valid(model) || model.parts.size() != 2 ||
        model.triangle_count() != 4608) {
        return false;
    }
    const auto analysis = icad::cad::analyze(*compilation.ir_project);
    if (analysis.parts.size() != 2 || !close(analysis.bounds.minimum[0], -40.0) ||
        !close(analysis.bounds.maximum[0], 140.0) ||
        icad::cad::distance(analysis.parts[0].bounds, analysis.parts[1].bounds) < 19.999) {
        return false;
    }

    for (std::size_t part_index = 0; part_index < model.parts.size(); ++part_index) {
        const auto& part = model.parts[part_index];
        const double center_x = part_index == 0 ? 0.0 : 100.0;
        std::set<long long> angular_samples;
        for (const auto& vertex : part.vertices) {
            const double local_x = vertex.x - center_x;
            const double radius = std::hypot(local_x, vertex.y);
            if (radius < 31.84 || radius > 40.001 || vertex.z < -tolerance ||
                vertex.z > 50.0 + tolerance) {
                return false;
            }
            double angle = std::atan2(vertex.y, local_x);
            if (angle < 0.0)
                angle += 2.0 * std::numbers::pi;
            angular_samples.insert(static_cast<long long>(std::llround(angle * 1.0e6)));
        }
        if (angular_samples.size() != 96)
            return false;

        // Every surface triangle must remain within one adjacent angular strip.
        // This rejects cap-like triangles spanning the hollow bore, the visual
        // failure that previously looked like plates inside the vessel.
        for (const auto& triangle : part.triangles) {
            double angles[3]{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& vertex = part.vertices[triangle[corner]];
                angles[corner] = std::atan2(vertex.y, vertex.x - center_x);
                if (angles[corner] < 0.0)
                    angles[corner] += 2.0 * std::numbers::pi;
            }
            const double maximum_span = std::max(
                {angular_distance(angles[0], angles[1]), angular_distance(angles[1], angles[2]),
                 angular_distance(angles[2], angles[0])});
            if (maximum_span > 2.0 * std::numbers::pi / 96.0 + tolerance)
                return false;
        }
    }
    return true;
}

} // namespace

auto main() -> int {
    if (!verify_box())
        return fail("known box geometry, bounds, area, or volume regressed");
    if (!verify_vessels())
        return fail("filleted annular vessel fidelity, smoothness, hollow bore, or spacing regressed");
    return 0;
}
