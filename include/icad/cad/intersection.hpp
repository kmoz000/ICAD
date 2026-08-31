#pragma once

#include "icad/cad/analysis.hpp"
#include "icad/cad/geometry.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace icad::cad {

struct Model;

inline constexpr double default_geometry_tolerance_mm = 1.0e-9;

struct Segment3 {
    Point3 start;
    Point3 end;
};

struct Ray3 {
    Point3 origin;
    Vector3 direction;
};

struct Plane3 {
    Point3 point;
    Vector3 normal;
};

struct Triangle3 {
    std::array<Point3, 3> points;
};

enum class IntersectionKind {
    none,
    point,
    segment,
    coplanar_overlap,
};

struct IntersectionResult {
    IntersectionKind kind{IntersectionKind::none};
    Point3 first{};
    Point3 second{};
    double parameter{};

    [[nodiscard]] auto intersects() const -> bool { return kind != IntersectionKind::none; }
};

struct OverlapPair {
    std::size_t first{};
    std::size_t second{};

    auto operator==(const OverlapPair&) const -> bool = default;
};

class SpatialIndex {
  public:
    explicit SpatialIndex(std::span<const Bounds> bounds,
                          double tolerance_mm = default_geometry_tolerance_mm);

    [[nodiscard]] auto query(const Bounds& bounds) const -> std::vector<std::size_t>;
    [[nodiscard]] auto overlap_pairs() const -> std::vector<OverlapPair>;
    [[nodiscard]] auto size() const -> std::size_t { return entries_.size(); }

  private:
    struct Entry {
        Bounds bounds;
        std::size_t index{};
    };

    std::vector<Entry> entries_;
    std::vector<double> prefix_maximum_x_;
    double tolerance_mm_{};
};

struct IntersectionAnalysis {
    std::size_t part_pair_candidates{};
    std::size_t triangle_pair_candidates{};
    std::size_t intersecting_part_pairs{};
    std::size_t intersecting_triangle_pairs{};
    std::size_t penetrating_part_pairs{};
    std::size_t declared_engagement_part_pairs{};
    std::size_t unintended_penetrating_part_pairs{};
    std::size_t contained_part_pairs{};
    std::size_t surface_contact_only_part_pairs{};
    struct BodyContact {
        std::string first_body;
        std::string second_body;
        std::size_t part_pair_candidates{};
        std::size_t triangle_pair_candidates{};
        std::size_t intersecting_part_pairs{};
        std::size_t intersecting_triangle_pairs{};
        std::size_t penetrating_part_pairs{};
        std::size_t contained_part_pairs{};
        std::size_t surface_contact_only_part_pairs{};
        bool has_witness_point{};
        Point3 witness_point{};
        bool declared_connection{};
        std::string connection_name;
        std::string connection_method;
        std::string connection_standard;
    };
    std::vector<BodyContact> body_contacts;
};

[[nodiscard]] auto valid(const Bounds& bounds) -> bool;
[[nodiscard]] auto overlaps(const Bounds& first, const Bounds& second,
                            double tolerance_mm = default_geometry_tolerance_mm) -> bool;
[[nodiscard]] auto bounds_of(const Triangle3& triangle) -> Bounds;
[[nodiscard]] auto intersect(const Segment3& segment, const Plane3& plane,
                             double tolerance_mm = default_geometry_tolerance_mm)
    -> IntersectionResult;
[[nodiscard]] auto intersect(const Ray3& ray, const Triangle3& triangle,
                             double tolerance_mm = default_geometry_tolerance_mm)
    -> IntersectionResult;
[[nodiscard]] auto intersect(const Triangle3& first, const Triangle3& second,
                             double tolerance_mm = default_geometry_tolerance_mm)
    -> IntersectionResult;
[[nodiscard]] auto analyze_intersections(const compiler::ir::Project& project,
                                         double tolerance_mm = default_geometry_tolerance_mm)
    -> IntersectionAnalysis;
[[nodiscard]] auto analyze_intersections(const compiler::ir::Project& project,
                                         const Model& model,
                                         double tolerance_mm = default_geometry_tolerance_mm)
    -> IntersectionAnalysis;

} // namespace icad::cad
