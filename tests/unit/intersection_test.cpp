#include "icad/cad/intersection.hpp"
#include "icad/compiler/compiler.hpp"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

auto close(double first, double second) -> bool { return std::abs(first - second) < 1.0e-9; }

} // namespace

auto main() -> int {
    using namespace icad::cad;

    const std::vector<Bounds> boxes{
        {{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}},
        {{0.5, 0.5, 0.5}, {2.0, 2.0, 2.0}},
        {{3.0, 3.0, 3.0}, {4.0, 4.0, 4.0}},
        {{2.0, 0.0, 0.0}, {3.0, 1.0, 1.0}},
    };
    const SpatialIndex index{boxes};
    const std::vector<OverlapPair> expected{{0, 1}, {1, 3}};
    if (index.size() != boxes.size() || index.overlap_pairs() != expected ||
        index.query({{0.75, 0.75, 0.75}, {0.8, 0.8, 0.8}}) !=
            std::vector<std::size_t>{0, 1}) {
        return fail("deterministic AABB index failed");
    }
    if (!overlaps(boxes[0], boxes[1]) || overlaps(boxes[0], boxes[2]))
        return fail("AABB overlap predicate failed");
    const std::vector<Bounds> intervals{
        {{0.0, 0.0, 0.0}, {100.0, 1.0, 1.0}},
        {{1.0, 0.0, 0.0}, {2.0, 1.0, 1.0}},
        {{80.0, 0.0, 0.0}, {81.0, 1.0, 1.0}},
    };
    const SpatialIndex interval_index{intervals};
    if (interval_index.query({{50.0, 0.0, 0.0}, {51.0, 1.0, 1.0}}) !=
        std::vector<std::size_t>{0})
        return fail("AABB prefix pruning skipped a long overlapping interval");

    const Plane3 plane{{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    const auto plane_hit = intersect(Segment3{{0.0, 0.0, -2.0}, {0.0, 0.0, 2.0}}, plane);
    if (plane_hit.kind != IntersectionKind::point || !close(plane_hit.first.z, 0.0) ||
        !close(plane_hit.parameter, 0.5)) {
        return fail("segment-plane crossing failed");
    }
    if (intersect(Segment3{{0.0, 0.0, 2.0}, {1.0, 0.0, 2.0}}, plane).intersects())
        return fail("parallel segment-plane miss failed");
    if (intersect(Segment3{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}}, plane).kind !=
        IntersectionKind::coplanar_overlap) {
        return fail("coplanar segment-plane classification failed");
    }

    const Triangle3 base{{{{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}}}};
    const auto ray_hit = intersect(Ray3{{0.5, 0.5, 2.0}, {0.0, 0.0, -1.0}}, base);
    if (ray_hit.kind != IntersectionKind::point || !close(ray_hit.parameter, 2.0) ||
        intersect(Ray3{{3.0, 3.0, 2.0}, {0.0, 0.0, -1.0}}, base).intersects()) {
        return fail("ray-triangle classification failed");
    }

    const Triangle3 crossing{{{{0.5, -0.5, -1.0}, {0.5, 1.5, 1.0},
                                {0.5, 1.5, -1.0}}}};
    if (intersect(base, crossing).kind != IntersectionKind::segment)
        return fail("non-coplanar triangle intersection failed");
    const Triangle3 coplanar{{{{0.25, 0.25, 0.0}, {1.0, 0.25, 0.0},
                                {0.25, 1.0, 0.0}}}};
    if (intersect(base, coplanar).kind != IntersectionKind::coplanar_overlap)
        return fail("coplanar triangle overlap failed");
    const Triangle3 shared_edge{{{{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0},
                                   {1.0, -1.0, 0.0}}}};
    if (intersect(base, shared_edge).kind != IntersectionKind::coplanar_overlap)
        return fail("shared triangle edge handling failed");
    const Triangle3 shared_vertex{{{{0.0, 0.0, 0.0}, {-1.0, 0.0, 1.0},
                                     {0.0, -1.0, 1.0}}}};
    if (intersect(base, shared_vertex).kind != IntersectionKind::point)
        return fail("shared triangle vertex handling failed");
    const Triangle3 separate{{{{3.0, 3.0, 0.0}, {4.0, 3.0, 0.0}, {3.0, 4.0, 0.0}}}};
    if (intersect(base, separate).intersects())
        return fail("coplanar triangle miss failed");
    const Triangle3 degenerate{{{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}}}};
    if (intersect(base, degenerate).intersects())
        return fail("degenerate triangle handling failed");

    constexpr std::string_view source =
        "PROJECT Intersections\nUNITS mm\n"
        "BODY first\nFEATURE one\nTYPE BOX\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\nEND\nEND\n"
        "BODY second\nFEATURE two\nTYPE BOX\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\n"
        "ORIGIN_X 1 mm\nORIGIN_Y 1 mm\nORIGIN_Z 1 mm\nEND\nEND\n";
    const auto compilation = icad::compiler::compile(source);
    if (!compilation.ok())
        return fail("intersection analysis fixture did not compile");
    const auto analysis = analyze_intersections(*compilation.ir_project);
    if (analysis.part_pair_candidates != 1 || analysis.triangle_pair_candidates == 0 ||
        analysis.intersecting_part_pairs != 1 || analysis.intersecting_triangle_pairs == 0 ||
        analysis.penetrating_part_pairs != 1 ||
        analysis.body_contacts.size() != 1 ||
        analysis.body_contacts.front().first_body != "first" ||
        analysis.body_contacts.front().second_body != "second" ||
        analysis.body_contacts.front().intersecting_part_pairs != 1) {
        return fail("project intersection analysis failed");
    }

    constexpr std::string_view containment_source =
        "PROJECT Containment\nUNITS mm\n"
        "BODY outer\nFEATURE box\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\nEND\nEND\n"
        "BODY inner\nFEATURE box\nTYPE BOX\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\n"
        "ORIGIN_X 4 mm\nORIGIN_Y 4 mm\nORIGIN_Z 4 mm\nEND\nEND\n";
    const auto containment = icad::compiler::compile(containment_source);
    if (!containment.ok())
        return fail("containment analysis fixture did not compile");
    const auto contained = analyze_intersections(*containment.ir_project);
    if (contained.penetrating_part_pairs != 1 || contained.contained_part_pairs != 1 ||
        contained.intersecting_part_pairs != 0) {
        return fail("solid containment was not classified as volume interference");
    }

    constexpr std::string_view cavity_source =
        "PROJECT Cavity\nUNITS mm\n"
        "BODY housing\nFEATURE stock\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\n"
        "END\nFEATURE cavity\nTYPE BOX\nOPERATION CUT\nWIDTH 4 mm\nDEPTH 4 mm\n"
        "HEIGHT 12 mm\nORIGIN_X 3 mm\nORIGIN_Y 3 mm\nORIGIN_Z -1 mm\nEND\nEND\n"
        "BODY insert\nFEATURE solid\nTYPE BOX\nWIDTH 1 mm\nDEPTH 1 mm\nHEIGHT 1 mm\n"
        "ORIGIN_X 4 mm\nORIGIN_Y 4 mm\nORIGIN_Z 4 mm\nEND\nEND\n";
    const auto cavity = icad::compiler::compile(cavity_source);
    if (!cavity.ok() || analyze_intersections(*cavity.ir_project).penetrating_part_pairs != 0)
        return fail("solid inside a machined cavity was misclassified as material containment");

    constexpr std::string_view touching_source =
        "PROJECT Touching\nUNITS mm\n"
        "BODY left\nFEATURE box\nTYPE BOX\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\nEND\nEND\n"
        "BODY right\nFEATURE box\nTYPE BOX\nWIDTH 2 mm\nDEPTH 2 mm\nHEIGHT 2 mm\n"
        "ORIGIN_X 2 mm\nEND\nEND\n";
    const auto touching = icad::compiler::compile(touching_source);
    if (!touching.ok())
        return fail("surface-contact fixture did not compile");
    const auto contact = analyze_intersections(*touching.ir_project);
    if (contact.penetrating_part_pairs != 0 || contact.surface_contact_only_part_pairs != 1) {
        return fail("surface-only contact was misclassified as solid interference");
    }
    return 0;
}
