#pragma once

#include "icad/cad/intersection.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace icad::cad {

struct DistanceQuery {
    bool found{};
    std::string first_body;
    std::string second_body;
    double distance_mm{};
    Point3 first_point;
    Point3 second_point;
    std::string representation{"exactPolyhedral"};
};

struct SectionSegment {
    std::string body;
    std::string part;
    Segment3 segment;
};

struct SectionQuery {
    Plane3 plane;
    double tolerance_mm{};
    std::string representation{"polyhedralBoundary"};
    std::vector<SectionSegment> segments;
};

// Exact Euclidean distance between the validated polyhedral boundaries used by
// STEP/STL/OBJ and the viewer. Curved analytic source may have a tessellated
// delivery boundary, so representation is reported explicitly.
[[nodiscard]] auto exact_polyhedral_distance(const compiler::ir::Project& project,
                                             std::string_view first_body,
                                             std::string_view second_body) -> DistanceQuery;

// Intersects a plane with all parts, or one named body when body is non-empty.
[[nodiscard]] auto section(const compiler::ir::Project& project, const Plane3& plane,
                           std::string_view body = {}) -> SectionQuery;

} // namespace icad::cad
