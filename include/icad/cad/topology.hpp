#pragma once

#include "icad/cad/geometry.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace icad::cad {

enum class CurveKind { line, circle };
enum class SurfaceKind { plane, cylinder, cone, sphere };

struct AnalyticCurve {
    CurveKind kind{CurveKind::line};
    Point3 origin;
    // Unit tangent for lines; unit parameter-zero reference direction for circles.
    Vector3 direction{1.0, 0.0, 0.0};
    Vector3 axis{0.0, 0.0, 1.0};
    double radius{};
    double parameter_start{};
    double parameter_end{1.0};
};

struct AnalyticSurface {
    SurfaceKind kind{SurfaceKind::plane};
    Point3 origin;
    Vector3 axis{0.0, 0.0, 1.0};
    double radius{};
    double semi_angle_radians{};
};

struct TopologyVertex {
    std::string id;
    Point3 point;
};

struct TopologyEdge {
    std::string id;
    std::string start_vertex;
    std::string end_vertex;
    AnalyticCurve curve;
    bool closed{};
};

struct OrientedEdge {
    std::string edge;
    bool reversed{};
};

struct Wire {
    std::string id;
    std::vector<OrientedEdge> edges;
};

struct Face {
    std::string id;
    AnalyticSurface surface;
    std::vector<Wire> boundaries;
};

struct Shell {
    std::string id;
    std::vector<std::string> faces;
};

struct SolidTopology {
    std::string id;
    std::string body;
    std::string feature;
    std::string feature_type;
    std::vector<TopologyVertex> vertices;
    std::vector<TopologyEdge> edges;
    std::vector<Face> faces;
    Shell shell;

    [[nodiscard]] auto euler_characteristic() const -> std::ptrdiff_t;
};

struct TopologyModel {
    std::vector<SolidTopology> solids;

    [[nodiscard]] auto vertex_count() const -> std::size_t;
    [[nodiscard]] auto edge_count() const -> std::size_t;
    [[nodiscard]] auto wire_count() const -> std::size_t;
    [[nodiscard]] auto face_count() const -> std::size_t;
};

struct TopologyIssue {
    std::string code;
    std::string entity;
    std::string message;
};

struct TopologyValidation {
    std::vector<TopologyIssue> issues;

    [[nodiscard]] auto valid() const noexcept -> bool { return issues.empty(); }
};

[[nodiscard]] auto curve_kind_name(CurveKind kind) -> std::string_view;
[[nodiscard]] auto surface_kind_name(SurfaceKind kind) -> std::string_view;
[[nodiscard]] auto build_topology(const compiler::ir::Project& project) -> TopologyModel;
[[nodiscard]] auto validate_topology(const TopologyModel& model) -> TopologyValidation;

} // namespace icad::cad
