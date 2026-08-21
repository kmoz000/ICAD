#include "icad/cad/topology.hpp"

#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace icad::cad {
namespace {

constexpr double tolerance = 1e-9;

[[nodiscard]] auto property(const compiler::ir::Feature& feature, std::string_view name,
                            double fallback = 0.0) -> double {
    const auto found = std::ranges::find(feature.properties, name, &compiler::ir::Property::name);
    return found == feature.properties.end() ? fallback : found->value.value;
}

[[nodiscard]] auto subtract(const Point3& first, const Point3& second) -> Vector3 {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

[[nodiscard]] auto magnitude(const Vector3& vector) -> double {
    return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

[[nodiscard]] auto dot(const Vector3& first, const Vector3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto cross(const Vector3& first, const Vector3& second) -> Vector3 {
    return {first.y * second.z - first.z * second.y, first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

[[nodiscard]] auto normalized(const Vector3& vector) -> Vector3 {
    const double length = magnitude(vector);
    return length <= tolerance ? Vector3{}
                               : Vector3{vector.x / length, vector.y / length, vector.z / length};
}

[[nodiscard]] auto same_point(const compiler::ir::Point2& first, const compiler::ir::Point2& second)
    -> bool {
    return std::hypot(first.x_mm - second.x_mm, first.y_mm - second.y_mm) <= tolerance;
}

auto rotate_x(Point3& point, double radians) -> void {
    const double y = point.y * std::cos(radians) - point.z * std::sin(radians);
    const double z = point.y * std::sin(radians) + point.z * std::cos(radians);
    point.y = y;
    point.z = z;
}

auto rotate_y(Point3& point, double radians) -> void {
    const double x = point.x * std::cos(radians) + point.z * std::sin(radians);
    const double z = -point.x * std::sin(radians) + point.z * std::cos(radians);
    point.x = x;
    point.z = z;
}

auto rotate_z(Point3& point, double radians) -> void {
    const double x = point.x * std::cos(radians) - point.y * std::sin(radians);
    const double y = point.x * std::sin(radians) + point.y * std::cos(radians);
    point.x = x;
    point.y = y;
}

struct Transform {
    double x_radians{};
    double y_radians{};
    double z_radians{};
    Point3 translation;

    [[nodiscard]] auto vector(Vector3 value) const -> Vector3 {
        rotate_x(value, x_radians);
        rotate_y(value, y_radians);
        rotate_z(value, z_radians);
        return value;
    }

    [[nodiscard]] auto point(Point3 value) const -> Point3 {
        value = vector(value);
        value.x += translation.x;
        value.y += translation.y;
        value.z += translation.z;
        return value;
    }
};

[[nodiscard]] auto feature_transform(const compiler::ir::Feature& feature) -> Transform {
    constexpr double radians = std::numbers::pi / 180.0;
    return {property(feature, "ROTATION_X") * radians,
            property(feature, "ROTATION_Y") * radians,
            property(feature, "ROTATION_Z") * radians,
            {property(feature, "ORIGIN_X"), property(feature, "ORIGIN_Y"),
             property(feature, "ORIGIN_Z")}};
}

[[nodiscard]] auto body_transform(const compiler::ir::BodyPose& pose) -> Transform {
    constexpr double radians = std::numbers::pi / 180.0;
    return {pose.transform.rotation_deg[0] * radians,
            pose.transform.rotation_deg[1] * radians,
            pose.transform.rotation_deg[2] * radians,
            {pose.transform.position_mm[0], pose.transform.position_mm[1],
             pose.transform.position_mm[2]}};
}

[[nodiscard]] auto vertex_id(const SolidTopology& solid, std::string_view name) -> std::string {
    return solid.id + "/vertex." + std::string{name};
}

[[nodiscard]] auto edge_id(const SolidTopology& solid, std::string_view name) -> std::string {
    return solid.id + "/edge." + std::string{name};
}

[[nodiscard]] auto face_id(const SolidTopology& solid, std::string_view name) -> std::string {
    return solid.id + "/face." + std::string{name};
}

auto add_vertex(SolidTopology& solid, std::string name, Point3 point) -> std::string {
    auto id = vertex_id(solid, name);
    solid.vertices.push_back({id, point});
    return id;
}

[[nodiscard]] auto line_curve(const Point3& first, const Point3& second) -> AnalyticCurve {
    const auto delta = subtract(second, first);
    return {CurveKind::line, first, normalized(delta), {0.0, 0.0, 1.0}, 0.0, 0.0, magnitude(delta)};
}

[[nodiscard]] auto circle_curve(Point3 center, Vector3 axis, double radius, double start = 0.0,
                                double end = 2.0 * std::numbers::pi,
                                Vector3 reference = {1.0, 0.0, 0.0}) -> AnalyticCurve {
    return {CurveKind::circle, center, normalized(reference), normalized(axis), radius, start, end};
}

auto add_edge(SolidTopology& solid, std::string name, std::string start, std::string end,
              AnalyticCurve curve, bool closed = false) -> std::string {
    auto id = edge_id(solid, name);
    solid.edges.push_back({id, std::move(start), std::move(end), curve, closed});
    return id;
}

auto add_face(SolidTopology& solid, std::string name, AnalyticSurface surface,
              std::vector<OrientedEdge> edges) -> std::string {
    auto id = face_id(solid, name);
    Wire wire{id + "/wire.outer", std::move(edges)};
    solid.faces.push_back({id, surface, {std::move(wire)}});
    solid.shell.faces.push_back(id);
    return id;
}

auto apply_transform(SolidTopology& solid, const Transform& transform) -> void {
    for (auto& vertex : solid.vertices) {
        vertex.point = transform.point(vertex.point);
    }
    for (auto& edge : solid.edges) {
        edge.curve.origin = transform.point(edge.curve.origin);
        edge.curve.direction = normalized(transform.vector(edge.curve.direction));
        edge.curve.axis = normalized(transform.vector(edge.curve.axis));
    }
    for (auto& face : solid.faces) {
        face.surface.origin = transform.point(face.surface.origin);
        face.surface.axis = normalized(transform.vector(face.surface.axis));
    }
}

[[nodiscard]] auto faceted_topology(const Part& part) -> SolidTopology {
    SolidTopology solid;
    solid.id = part.name;
    solid.body = part.body;
    solid.feature = part.name;
    solid.feature_type = part.feature_type.empty() ? "FACETED" : part.feature_type;
    solid.shell.id = solid.id + "/shell.outer";
    std::vector<std::string> vertex_ids;
    vertex_ids.reserve(part.vertices.size());
    for (std::size_t index = 0; index < part.vertices.size(); ++index)
        vertex_ids.push_back(add_vertex(solid, std::to_string(index), part.vertices[index]));

    std::map<std::pair<std::size_t, std::size_t>, std::string> edge_ids;
    for (std::size_t triangle_index = 0; triangle_index < part.triangles.size();
         ++triangle_index) {
        const auto& triangle = part.triangles[triangle_index];
        std::vector<OrientedEdge> boundary;
        boundary.reserve(3);
        for (std::size_t edge_index = 0; edge_index < 3; ++edge_index) {
            const auto start = triangle[edge_index];
            const auto end = triangle[(edge_index + 1) % 3];
            const auto key = std::minmax(start, end);
            auto found = edge_ids.find(key);
            if (found == edge_ids.end()) {
                const auto name = std::to_string(key.first) + "." + std::to_string(key.second);
                const auto edge_id = add_edge(solid, name, vertex_ids[key.first],
                                              vertex_ids[key.second],
                                              line_curve(part.vertices[key.first],
                                                         part.vertices[key.second]));
                found = edge_ids.emplace(key, edge_id).first;
            }
            boundary.push_back({found->second, start != key.first});
        }
        const auto& first = part.vertices[triangle[0]];
        const auto normal = normalized(cross(subtract(part.vertices[triangle[1]], first),
                                             subtract(part.vertices[triangle[2]], first)));
        add_face(solid, "triangle." + std::to_string(triangle_index),
                 {SurfaceKind::plane, first, normal, 0.0, 0.0}, std::move(boundary));
    }
    return solid;
}

[[nodiscard]] auto extrusion_topology(const std::string& prefix, const std::string& body,
                                      const compiler::ir::Feature& feature,
                                      const std::vector<compiler::ir::ProfileSegment>& segments,
                                      double height) -> SolidTopology {
    SolidTopology solid;
    solid.id = prefix;
    solid.body = body;
    solid.feature = feature.name;
    solid.feature_type = feature.type;
    solid.shell.id = prefix + "/shell.outer";
    const std::size_t count = segments.size();
    std::vector<std::string> bottom_vertices;
    std::vector<std::string> top_vertices;
    std::vector<std::string> bottom_edges;
    std::vector<std::string> top_edges;
    std::vector<std::string> vertical_edges;
    bottom_vertices.reserve(count);
    top_vertices.reserve(count);
    bottom_edges.reserve(count);
    top_edges.reserve(count);
    vertical_edges.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto suffix = std::to_string(index);
        const auto& start = segments[index].start;
        bottom_vertices.push_back(
            add_vertex(solid, "bottom." + suffix, {start.x_mm, start.y_mm, 0.0}));
        top_vertices.push_back(
            add_vertex(solid, "top." + suffix, {start.x_mm, start.y_mm, height}));
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto suffix = std::to_string(index);
        const std::size_t next = (index + 1) % count;
        const auto& segment = segments[index];
        const Point3 bottom_start{segment.start.x_mm, segment.start.y_mm, 0.0};
        const Point3 bottom_end{segment.end.x_mm, segment.end.y_mm, 0.0};
        const Point3 top_start{segment.start.x_mm, segment.start.y_mm, height};
        const Point3 top_end{segment.end.x_mm, segment.end.y_mm, height};
        AnalyticCurve bottom_curve;
        AnalyticCurve top_curve;
        if (segment.kind == compiler::ir::ProfileSegmentKind::line) {
            bottom_curve = line_curve(bottom_start, bottom_end);
            top_curve = line_curve(top_start, top_end);
        } else {
            const Vector3 reference = normalized({segment.start.x_mm - segment.center.x_mm,
                                                  segment.start.y_mm - segment.center.y_mm, 0.0});
            const Vector3 axis =
                segment.sweep_radians >= 0.0 ? Vector3{0.0, 0.0, 1.0} : Vector3{0.0, 0.0, -1.0};
            bottom_curve =
                circle_curve({segment.center.x_mm, segment.center.y_mm, 0.0}, axis,
                             segment.radius_mm, 0.0, std::abs(segment.sweep_radians), reference);
            top_curve =
                circle_curve({segment.center.x_mm, segment.center.y_mm, height}, axis,
                             segment.radius_mm, 0.0, std::abs(segment.sweep_radians), reference);
        }
        const bool closed_edge = count == 1 && same_point(segment.start, segment.end);
        bottom_edges.push_back(add_edge(solid, "bottom." + suffix, bottom_vertices[index],
                                        bottom_vertices[next], bottom_curve, closed_edge));
        top_edges.push_back(add_edge(solid, "top." + suffix, top_vertices[index],
                                     top_vertices[next], top_curve, closed_edge));
        vertical_edges.push_back(add_edge(solid, "vertical." + suffix, bottom_vertices[index],
                                          top_vertices[index],
                                          line_curve(bottom_start, top_start)));
    }
    std::vector<OrientedEdge> bottom_boundary;
    std::vector<OrientedEdge> top_boundary;
    bottom_boundary.reserve(count);
    top_boundary.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        bottom_boundary.push_back({bottom_edges[count - index - 1], true});
        top_boundary.push_back({top_edges[index], false});
    }
    add_face(solid, "bottom", {SurfaceKind::plane, {0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
             std::move(bottom_boundary));
    add_face(solid, "top", {SurfaceKind::plane, {0.0, 0.0, height}, {0.0, 0.0, 1.0}},
             std::move(top_boundary));
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1) % count;
        const auto& segment = segments[index];
        AnalyticSurface side;
        if (segment.kind == compiler::ir::ProfileSegmentKind::line) {
            const double dx = segment.end.x_mm - segment.start.x_mm;
            const double dy = segment.end.y_mm - segment.start.y_mm;
            side = {SurfaceKind::plane,
                    {segment.start.x_mm, segment.start.y_mm, 0.0},
                    normalized({dy, -dx, 0.0})};
        } else {
            side = {SurfaceKind::cylinder,
                    {segment.center.x_mm, segment.center.y_mm, 0.0},
                    {0.0, 0.0, 1.0},
                    segment.radius_mm};
        }
        add_face(solid, "side." + std::to_string(index), side,
                 {{bottom_edges[index], false},
                  {vertical_edges[next], false},
                  {top_edges[index], true},
                  {vertical_edges[index], true}});
    }
    apply_transform(solid, feature_transform(feature));
    return solid;
}

[[nodiscard]] auto frustum_topology(const std::string& prefix, const std::string& body,
                                    const compiler::ir::Feature& feature, double bottom_radius,
                                    double top_radius, double height) -> SolidTopology {
    SolidTopology solid;
    solid.id = prefix;
    solid.body = body;
    solid.feature = feature.name;
    solid.feature_type = feature.type;
    solid.shell.id = prefix + "/shell.outer";
    const auto bottom = add_vertex(solid, "bottom.seam", {bottom_radius, 0.0, 0.0});
    const auto top = add_vertex(solid, "top.seam", {top_radius, 0.0, height});
    const auto bottom_ring =
        add_edge(solid, "bottom.ring", bottom, bottom,
                 circle_curve({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, bottom_radius), true);
    const auto top_ring =
        add_edge(solid, "top.ring", top, top,
                 circle_curve({0.0, 0.0, height}, {0.0, 0.0, 1.0}, top_radius), true);
    const Point3 seam_bottom{bottom_radius, 0.0, 0.0};
    const Point3 seam_top{top_radius, 0.0, height};
    const auto seam = add_edge(solid, "side.seam", bottom, top, line_curve(seam_bottom, seam_top));
    add_face(solid, "bottom", {SurfaceKind::plane, {0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
             {{bottom_ring, true}});
    add_face(solid, "top", {SurfaceKind::plane, {0.0, 0.0, height}, {0.0, 0.0, 1.0}},
             {{top_ring, false}});
    AnalyticSurface side;
    if (std::abs(top_radius - bottom_radius) <= tolerance) {
        side = {SurfaceKind::cylinder, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, bottom_radius};
    } else {
        const double apex_z = -bottom_radius * height / (top_radius - bottom_radius);
        side = {SurfaceKind::cone,
                {0.0, 0.0, apex_z},
                {0.0, 0.0, 1.0},
                0.0,
                std::atan(std::abs(top_radius - bottom_radius) / height)};
    }
    add_face(solid, "side", side,
             {{bottom_ring, false}, {seam, false}, {top_ring, true}, {seam, true}});
    apply_transform(solid, feature_transform(feature));
    return solid;
}

[[nodiscard]] auto sphere_topology(const std::string& prefix, const std::string& body,
                                   const compiler::ir::Feature& feature, double radius)
    -> SolidTopology {
    SolidTopology solid;
    solid.id = prefix;
    solid.body = body;
    solid.feature = feature.name;
    solid.feature_type = feature.type;
    solid.shell.id = prefix + "/shell.outer";
    const auto north = add_vertex(solid, "north", {0.0, 0.0, radius});
    const auto south = add_vertex(solid, "south", {0.0, 0.0, -radius});
    const auto east =
        add_edge(solid, "meridian.east", north, south,
                 circle_curve({}, {0.0, 1.0, 0.0}, radius, 0.0, std::numbers::pi, {0.0, 0.0, 1.0}));
    const auto west = add_edge(solid, "meridian.west", south, north,
                               circle_curve({}, {0.0, 1.0, 0.0}, radius, std::numbers::pi,
                                            2.0 * std::numbers::pi, {0.0, 0.0, 1.0}));
    const AnalyticSurface surface{SurfaceKind::sphere, {}, {0.0, 0.0, 1.0}, radius};
    add_face(solid, "hemisphere.east", surface, {{east, false}, {west, false}});
    add_face(solid, "hemisphere.west", surface, {{west, true}, {east, true}});
    apply_transform(solid, feature_transform(feature));
    return solid;
}

[[nodiscard]] auto revolved_surface(const compiler::ir::Point2& first,
                                    const compiler::ir::Point2& second) -> AnalyticSurface {
    const double delta_radius = second.x_mm - first.x_mm;
    const double delta_z = second.y_mm - first.y_mm;
    if (std::abs(delta_z) <= tolerance) {
        return {SurfaceKind::plane,
                {0.0, 0.0, first.y_mm},
                {0.0, 0.0, delta_radius >= 0.0 ? 1.0 : -1.0}};
    }
    if (std::abs(delta_radius) <= tolerance) {
        return {SurfaceKind::cylinder, {0.0, 0.0, first.y_mm}, {0.0, 0.0, 1.0}, first.x_mm};
    }
    const double apex_z = first.y_mm - first.x_mm * delta_z / delta_radius;
    return {SurfaceKind::cone,
            {0.0, 0.0, apex_z},
            {0.0, 0.0, 1.0},
            0.0,
            std::atan(std::abs(delta_radius / delta_z))};
}

[[nodiscard]] auto revolve_topology(const std::string& prefix, const std::string& body,
                                    const compiler::ir::Feature& feature,
                                    const compiler::ir::Profile& profile) -> SolidTopology {
    SolidTopology solid;
    solid.id = prefix;
    solid.body = body;
    solid.feature = feature.name;
    solid.feature_type = feature.type;
    solid.shell.id = prefix + "/shell.outer";
    const std::size_t count = profile.points.size();
    std::vector<std::string> vertices;
    std::vector<std::string> rings;
    std::vector<std::string> seams;
    vertices.reserve(count);
    rings.reserve(count);
    seams.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& point = profile.points[index];
        vertices.push_back(
            add_vertex(solid, "seam." + std::to_string(index), {point.x_mm, 0.0, point.y_mm}));
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& point = profile.points[index];
        const std::size_t next = (index + 1) % count;
        rings.push_back(
            add_edge(solid, "ring." + std::to_string(index), vertices[index], vertices[index],
                     circle_curve({0.0, 0.0, point.y_mm}, {0.0, 0.0, 1.0}, point.x_mm), true));
        const Point3 start{point.x_mm, 0.0, point.y_mm};
        const Point3 end{profile.points[next].x_mm, 0.0, profile.points[next].y_mm};
        seams.push_back(add_edge(solid, "seam." + std::to_string(index), vertices[index],
                                 vertices[next], line_curve(start, end)));
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t next = (index + 1) % count;
        add_face(solid, "revolved." + std::to_string(index),
                 revolved_surface(profile.points[index], profile.points[next]),
                 {{rings[index], false},
                  {seams[index], false},
                  {rings[next], true},
                  {seams[index], true}});
    }
    apply_transform(solid, feature_transform(feature));
    return solid;
}

[[nodiscard]] auto finite(const Point3& point) -> bool {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] auto near(const Point3& first, const Point3& second) -> bool {
    return magnitude(subtract(first, second)) <= 1e-7;
}

[[nodiscard]] auto curve_point(const AnalyticCurve& curve, double parameter) -> Point3 {
    if (curve.kind == CurveKind::line) {
        return {curve.origin.x + curve.direction.x * parameter,
                curve.origin.y + curve.direction.y * parameter,
                curve.origin.z + curve.direction.z * parameter};
    }
    const auto perpendicular = cross(curve.axis, curve.direction);
    return {curve.origin.x + curve.radius * (curve.direction.x * std::cos(parameter) +
                                             perpendicular.x * std::sin(parameter)),
            curve.origin.y + curve.radius * (curve.direction.y * std::cos(parameter) +
                                             perpendicular.y * std::sin(parameter)),
            curve.origin.z + curve.radius * (curve.direction.z * std::cos(parameter) +
                                             perpendicular.z * std::sin(parameter))};
}

[[nodiscard]] auto point_on_surface(const Point3& point, const AnalyticSurface& surface) -> bool {
    const auto delta = subtract(point, surface.origin);
    const double axial = dot(delta, surface.axis);
    const Vector3 radial{delta.x - surface.axis.x * axial, delta.y - surface.axis.y * axial,
                         delta.z - surface.axis.z * axial};
    const double radial_distance = magnitude(radial);
    double residual = 0.0;
    switch (surface.kind) {
    case SurfaceKind::plane:
        residual = std::abs(axial);
        break;
    case SurfaceKind::cylinder:
        residual = std::abs(radial_distance - surface.radius);
        break;
    case SurfaceKind::cone:
        residual =
            std::abs(radial_distance - std::abs(axial) * std::tan(surface.semi_angle_radians));
        break;
    case SurfaceKind::sphere:
        residual = std::abs(magnitude(delta) - surface.radius);
        break;
    }
    const double scale = std::max({1.0, magnitude(delta), surface.radius});
    return residual <= 1e-7 * scale;
}

auto issue(TopologyValidation& validation, std::string code, std::string entity,
           std::string message) -> void {
    validation.issues.push_back({std::move(code), std::move(entity), std::move(message)});
}

} // namespace

auto SolidTopology::euler_characteristic() const -> std::ptrdiff_t {
    return static_cast<std::ptrdiff_t>(vertices.size()) -
           static_cast<std::ptrdiff_t>(edges.size()) + static_cast<std::ptrdiff_t>(faces.size());
}

auto TopologyModel::vertex_count() const -> std::size_t {
    std::size_t result = 0;
    for (const auto& solid : solids)
        result += solid.vertices.size();
    return result;
}

auto TopologyModel::edge_count() const -> std::size_t {
    std::size_t result = 0;
    for (const auto& solid : solids)
        result += solid.edges.size();
    return result;
}

auto TopologyModel::wire_count() const -> std::size_t {
    std::size_t result = 0;
    for (const auto& solid : solids) {
        for (const auto& face : solid.faces)
            result += face.boundaries.size();
    }
    return result;
}

auto TopologyModel::face_count() const -> std::size_t {
    std::size_t result = 0;
    for (const auto& solid : solids)
        result += solid.faces.size();
    return result;
}

auto curve_kind_name(CurveKind kind) -> std::string_view {
    switch (kind) {
    case CurveKind::line:
        return "line";
    case CurveKind::circle:
        return "circle";
    }
    return "unknown";
}

auto surface_kind_name(SurfaceKind kind) -> std::string_view {
    switch (kind) {
    case SurfaceKind::plane:
        return "plane";
    case SurfaceKind::cylinder:
        return "cylinder";
    case SurfaceKind::cone:
        return "cone";
    case SurfaceKind::sphere:
        return "sphere";
    }
    return "unknown";
}

auto build_topology(const compiler::ir::Project& project) -> TopologyModel {
    TopologyModel model;
    const auto delivery_model = build_model(project);
    for (const auto& body : project.bodies) {
        const bool curved_revolve = std::ranges::any_of(body.features, [&](const auto& feature) {
            if (feature.type != "REVOLVE")
                return false;
            const auto profile =
                std::ranges::find(project.profiles, feature.profile, &compiler::ir::Profile::name);
            return profile != project.profiles.end() &&
                   std::ranges::any_of(profile->segments, [](const auto& segment) {
                       return segment.kind == compiler::ir::ProfileSegmentKind::circular_arc;
                   });
        });
        const bool faceted_body = curved_revolve ||
            std::ranges::any_of(body.features, [](const auto& feature) {
            return feature.operation != compiler::ir::FeatureOperation::create ||
                   feature.type == "CHAMFER" || feature.type == "FILLET" ||
                   feature.type == "LINEAR_PATTERN" || feature.type == "MIRROR" ||
                   feature.type == "SWEEP" || feature.type == "LOFT" ||
                   feature.type == "FREEFORM";
        });
        if (faceted_body) {
            for (const auto& part : delivery_model.parts) {
                if (part.body == body.name)
                    model.solids.push_back(faceted_topology(part));
            }
            continue;
        }
        for (const auto& feature : body.features) {
            const auto first_new_solid = model.solids.size();
            const std::string prefix = body.name + "/" + feature.name;
            if (feature.type == "BOX") {
                const std::vector<compiler::ir::Point2> points{
                    {0.0, 0.0},
                    {property(feature, "WIDTH"), 0.0},
                    {property(feature, "WIDTH"), property(feature, "DEPTH")},
                    {0.0, property(feature, "DEPTH")}};
                std::vector<compiler::ir::ProfileSegment> segments;
                for (std::size_t index = 0; index < points.size(); ++index) {
                    segments.push_back({compiler::ir::ProfileSegmentKind::line,
                                        points[index],
                                        points[(index + 1) % points.size()],
                                        {},
                                        0.0,
                                        0.0});
                }
                model.solids.push_back(extrusion_topology(prefix, body.name, feature, segments,
                                                          property(feature, "HEIGHT")));
            } else if (feature.type == "CYLINDER") {
                model.solids.push_back(
                    frustum_topology(prefix, body.name, feature, property(feature, "RADIUS"),
                                     property(feature, "RADIUS"), property(feature, "HEIGHT")));
            } else if (feature.type == "CONE") {
                model.solids.push_back(
                    frustum_topology(prefix, body.name, feature, property(feature, "RADIUS1"),
                                     property(feature, "RADIUS2"), property(feature, "HEIGHT")));
            } else if (feature.type == "SPHERE") {
                model.solids.push_back(
                    sphere_topology(prefix, body.name, feature, property(feature, "RADIUS")));
            } else {
                const auto profile = std::ranges::find(project.profiles, feature.profile,
                                                       &compiler::ir::Profile::name);
                if (profile != project.profiles.end() && feature.type == "EXTRUDE") {
                    model.solids.push_back(extrusion_topology(prefix, body.name, feature,
                                                              profile->segments,
                                                              property(feature, "HEIGHT")));
                } else if (profile != project.profiles.end() && feature.type == "REVOLVE") {
                    model.solids.push_back(revolve_topology(prefix, body.name, feature, *profile));
                } else {
                    SolidTopology unsupported;
                    unsupported.id = prefix;
                    unsupported.body = body.name;
                    unsupported.feature = feature.name;
                    unsupported.feature_type = feature.type;
                    unsupported.shell.id = prefix + "/shell.outer";
                    model.solids.push_back(std::move(unsupported));
                }
            }
            const auto pose =
                std::ranges::find(project.poses, body.name, &compiler::ir::BodyPose::body);
            if (pose != project.poses.end()) {
                for (std::size_t index = first_new_solid; index < model.solids.size(); ++index) {
                    apply_transform(model.solids[index], body_transform(*pose));
                }
            }
        }
    }
    for (const auto& instance : project.instances) {
        for (const auto& part : delivery_model.parts) {
            if (part.body == instance.name)
                model.solids.push_back(faceted_topology(part));
        }
    }
    return model;
}

auto validate_topology(const TopologyModel& model) -> TopologyValidation {
    TopologyValidation validation;
    if (model.solids.empty()) {
        issue(validation, "ICAD-G0001", "project", "topology contains no solids");
        return validation;
    }
    std::unordered_set<std::string> solid_ids;
    for (const auto& solid : model.solids) {
        if (solid.id.empty() || !solid_ids.insert(solid.id).second) {
            issue(validation, "ICAD-G0002", solid.id, "solid ID is empty or duplicated");
        }
        if (solid.vertices.empty() || solid.edges.empty() || solid.faces.empty() ||
            solid.shell.id.empty()) {
            issue(validation, "ICAD-G0003", solid.id, "solid topology is incomplete");
            continue;
        }
        std::unordered_map<std::string, const TopologyVertex*> vertices;
        for (const auto& vertex : solid.vertices) {
            if (vertex.id.empty() || !vertices.emplace(vertex.id, &vertex).second) {
                issue(validation, "ICAD-G0004", vertex.id, "vertex ID is empty or duplicated");
            } else if (!finite(vertex.point)) {
                issue(validation, "ICAD-G0005", vertex.id, "vertex coordinates are not finite");
            }
        }
        std::unordered_map<std::string, const TopologyEdge*> edges;
        for (const auto& edge : solid.edges) {
            if (edge.id.empty() || !edges.emplace(edge.id, &edge).second) {
                issue(validation, "ICAD-G0006", edge.id, "edge ID is empty or duplicated");
                continue;
            }
            if (!vertices.contains(edge.start_vertex) || !vertices.contains(edge.end_vertex)) {
                issue(validation, "ICAD-G0007", edge.id, "edge references a missing vertex");
            }
            if (edge.closed != (edge.start_vertex == edge.end_vertex)) {
                issue(validation, "ICAD-G0008", edge.id,
                      "edge closure does not match its endpoint topology");
            }
            if (!finite(edge.curve.origin) || !finite(edge.curve.direction) ||
                !finite(edge.curve.axis) || !std::isfinite(edge.curve.parameter_start) ||
                !std::isfinite(edge.curve.parameter_end) ||
                edge.curve.parameter_end <= edge.curve.parameter_start) {
                issue(validation, "ICAD-G0009", edge.id, "curve definition is invalid");
            }
            if (edge.curve.kind == CurveKind::line &&
                std::abs(magnitude(edge.curve.direction) - 1.0) > tolerance) {
                issue(validation, "ICAD-G0010", edge.id, "line direction is not normalized");
            }
            if (edge.curve.kind == CurveKind::circle &&
                (edge.curve.radius <= 0.0 ||
                 std::abs(magnitude(edge.curve.axis) - 1.0) > tolerance ||
                 std::abs(magnitude(edge.curve.direction) - 1.0) > tolerance ||
                 std::abs(dot(edge.curve.axis, edge.curve.direction)) > tolerance)) {
                issue(validation, "ICAD-G0011", edge.id, "circle definition is invalid");
            }
            if (vertices.contains(edge.start_vertex) && vertices.contains(edge.end_vertex) &&
                (!near(curve_point(edge.curve, edge.curve.parameter_start),
                       vertices.at(edge.start_vertex)->point) ||
                 !near(curve_point(edge.curve, edge.curve.parameter_end),
                       vertices.at(edge.end_vertex)->point))) {
                issue(validation, "ICAD-G0023", edge.id,
                      "analytic curve endpoints do not match topology vertices");
            }
        }
        struct Uses {
            std::size_t total{};
            std::size_t forward{};
            std::size_t reverse{};
        };
        std::unordered_map<std::string, Uses> uses;
        std::unordered_set<std::string> face_ids;
        std::unordered_set<std::string> wire_ids;
        for (const auto& face : solid.faces) {
            if (face.id.empty() || !face_ids.insert(face.id).second) {
                issue(validation, "ICAD-G0012", face.id, "face ID is empty or duplicated");
            }
            if (!finite(face.surface.origin) || !finite(face.surface.axis) ||
                std::abs(magnitude(face.surface.axis) - 1.0) > tolerance ||
                !std::isfinite(face.surface.radius) ||
                !std::isfinite(face.surface.semi_angle_radians)) {
                issue(validation, "ICAD-G0013", face.id, "surface definition is invalid");
            }
            if ((face.surface.kind == SurfaceKind::cylinder ||
                 face.surface.kind == SurfaceKind::sphere) &&
                face.surface.radius <= 0.0) {
                issue(validation, "ICAD-G0014", face.id, "analytic surface radius is invalid");
            }
            if (face.surface.kind == SurfaceKind::cone &&
                (face.surface.semi_angle_radians <= 0.0 ||
                 face.surface.semi_angle_radians >= std::numbers::pi / 2.0)) {
                issue(validation, "ICAD-G0014", face.id, "conical surface angle is invalid");
            }
            if (face.boundaries.empty()) {
                issue(validation, "ICAD-G0015", face.id, "face has no boundary wire");
            }
            for (const auto& wire : face.boundaries) {
                if (wire.id.empty() || !wire_ids.insert(wire.id).second || wire.edges.empty()) {
                    issue(validation, "ICAD-G0016", wire.id,
                          "wire ID is empty or duplicated, or wire has no edges");
                    continue;
                }
                for (std::size_t index = 0; index < wire.edges.size(); ++index) {
                    const auto& use = wire.edges[index];
                    const auto found = edges.find(use.edge);
                    if (found == edges.end()) {
                        issue(validation, "ICAD-G0017", wire.id, "wire references a missing edge");
                        continue;
                    }
                    auto& count = uses[use.edge];
                    ++count.total;
                    use.reversed ? ++count.reverse : ++count.forward;
                    const auto& next_use = wire.edges[(index + 1) % wire.edges.size()];
                    const auto next_found = edges.find(next_use.edge);
                    if (next_found == edges.end())
                        continue;
                    const auto* edge = found->second;
                    const auto* next_edge = next_found->second;
                    for (const double parameter :
                         {edge->curve.parameter_start,
                          (edge->curve.parameter_start + edge->curve.parameter_end) / 2.0,
                          edge->curve.parameter_end}) {
                        if (!point_on_surface(curve_point(edge->curve, parameter), face.surface)) {
                            issue(validation, "ICAD-G0024", face.id,
                                  "boundary curve does not lie on its analytic surface");
                            break;
                        }
                    }
                    const auto& end = use.reversed ? edge->start_vertex : edge->end_vertex;
                    const auto& next_start =
                        next_use.reversed ? next_edge->end_vertex : next_edge->start_vertex;
                    if (end != next_start) {
                        issue(validation, "ICAD-G0018", wire.id,
                              "oriented wire is not topologically closed");
                    }
                }
            }
        }
        for (const auto& edge : solid.edges) {
            const auto found = uses.find(edge.id);
            if (found == uses.end() || found->second.total != 2 || found->second.forward != 1 ||
                found->second.reverse != 1) {
                issue(validation, "ICAD-G0019", edge.id,
                      "closed shell edge must have one forward and one reverse use");
            }
        }
        std::unordered_set<std::string> shell_faces;
        for (const auto& face : solid.shell.faces) {
            if (!face_ids.contains(face) || !shell_faces.insert(face).second) {
                issue(validation, "ICAD-G0020", solid.shell.id,
                      "shell references a missing or duplicate face");
            }
        }
        if (shell_faces.size() != face_ids.size()) {
            issue(validation, "ICAD-G0021", solid.shell.id,
                  "shell does not own every solid face exactly once");
        }
        const auto characteristic = solid.euler_characteristic();
        if (characteristic > 2 || characteristic % 2 != 0) {
            issue(validation, "ICAD-G0022", solid.id,
                  "Euler characteristic is invalid for a closed orientable shell");
        }
    }
    return validation;
}

} // namespace icad::cad
