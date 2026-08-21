#include "boolean.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace icad::cad {
namespace {

constexpr double epsilon = 1.0e-8;

[[nodiscard]] auto subtract(const Point3& first, const Point3& second) -> Vector3 {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

[[nodiscard]] auto add(const Point3& first, const Point3& second) -> Point3 {
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

[[nodiscard]] auto scale(const Point3& point, double factor) -> Point3 {
    return {point.x * factor, point.y * factor, point.z * factor};
}

[[nodiscard]] auto dot(const Vector3& first, const Vector3& second) -> double {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

[[nodiscard]] auto cross(const Vector3& first, const Vector3& second) -> Vector3 {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

[[nodiscard]] auto magnitude(const Vector3& vector) -> double {
    return std::sqrt(dot(vector, vector));
}

[[nodiscard]] auto normalized(const Vector3& vector) -> Vector3 {
    const double length = magnitude(vector);
    return length <= epsilon ? Vector3{}
                             : Vector3{vector.x / length, vector.y / length, vector.z / length};
}

struct Vertex {
    Point3 position;

    [[nodiscard]] auto interpolate(const Vertex& other, double amount) const -> Vertex {
        return {add(position, scale(subtract(other.position, position), amount))};
    }
};

struct Polygon;

struct Plane {
    Vector3 normal;
    double distance{};

    Plane() = default;
    Plane(const Point3& first, const Point3& second, const Point3& third)
        : normal{normalized(cross(subtract(second, first), subtract(third, first)))},
          distance{dot(normal, first)} {}

    auto flip() -> void {
        normal = scale(normal, -1.0);
        distance = -distance;
    }

    auto split(const Polygon& polygon, std::vector<Polygon>& coplanar_front,
               std::vector<Polygon>& coplanar_back, std::vector<Polygon>& front,
               std::vector<Polygon>& back) const -> void;
};

struct Polygon {
    std::vector<Vertex> vertices;
    Plane plane;

    Polygon() = default;
    explicit Polygon(std::vector<Vertex> source) : vertices{std::move(source)} {
        if (vertices.size() >= 3)
            plane = Plane{vertices[0].position, vertices[1].position, vertices[2].position};
    }

    auto flip() -> void {
        std::ranges::reverse(vertices);
        plane.flip();
    }
};

auto Plane::split(const Polygon& polygon, std::vector<Polygon>& coplanar_front,
                  std::vector<Polygon>& coplanar_back, std::vector<Polygon>& front,
                  std::vector<Polygon>& back) const -> void {
    constexpr int coplanar = 0;
    constexpr int in_front = 1;
    constexpr int behind = 2;
    constexpr int spanning = 3;
    int polygon_type = coplanar;
    std::vector<int> types;
    types.reserve(polygon.vertices.size());
    for (const auto& vertex : polygon.vertices) {
        const double offset = dot(normal, vertex.position) - distance;
        const int type = offset < -epsilon ? behind : offset > epsilon ? in_front : coplanar;
        polygon_type |= type;
        types.push_back(type);
    }
    if (polygon_type == coplanar) {
        (dot(normal, polygon.plane.normal) >= 0.0 ? coplanar_front : coplanar_back)
            .push_back(polygon);
        return;
    }
    if (polygon_type == in_front) {
        front.push_back(polygon);
        return;
    }
    if (polygon_type == behind) {
        back.push_back(polygon);
        return;
    }
    if (polygon_type != spanning)
        return;

    std::vector<Vertex> front_vertices;
    std::vector<Vertex> back_vertices;
    for (std::size_t index = 0; index < polygon.vertices.size(); ++index) {
        const std::size_t next = (index + 1) % polygon.vertices.size();
        const int type = types[index];
        const int next_type = types[next];
        const auto& vertex = polygon.vertices[index];
        const auto& next_vertex = polygon.vertices[next];
        if (type != behind)
            front_vertices.push_back(vertex);
        if (type != in_front)
            back_vertices.push_back(vertex);
        if ((type | next_type) != spanning)
            continue;
        const auto direction = subtract(next_vertex.position, vertex.position);
        const double denominator = dot(normal, direction);
        if (std::abs(denominator) <= epsilon)
            continue;
        const double amount = (distance - dot(normal, vertex.position)) / denominator;
        const auto intersection = vertex.interpolate(next_vertex, amount);
        front_vertices.push_back(intersection);
        back_vertices.push_back(intersection);
    }
    if (front_vertices.size() >= 3)
        front.emplace_back(std::move(front_vertices));
    if (back_vertices.size() >= 3)
        back.emplace_back(std::move(back_vertices));
}

class Node {
  public:
    Node() = default;
    explicit Node(const std::vector<Polygon>& source) { build(source); }

    auto invert() -> void {
        for (auto& polygon : polygons_)
            polygon.flip();
        if (plane_)
            plane_->flip();
        if (front_)
            front_->invert();
        if (back_)
            back_->invert();
        std::swap(front_, back_);
    }

    [[nodiscard]] auto clip_polygons(const std::vector<Polygon>& source) const
        -> std::vector<Polygon> {
        if (!plane_)
            return source;
        std::vector<Polygon> front;
        std::vector<Polygon> back;
        for (const auto& polygon : source)
            plane_->split(polygon, front, back, front, back);
        if (front_)
            front = front_->clip_polygons(front);
        if (back_)
            back = back_->clip_polygons(back);
        else
            back.clear();
        front.insert(front.end(), std::make_move_iterator(back.begin()),
                     std::make_move_iterator(back.end()));
        return front;
    }

    auto clip_to(const Node& other) -> void {
        polygons_ = other.clip_polygons(polygons_);
        if (front_)
            front_->clip_to(other);
        if (back_)
            back_->clip_to(other);
    }

    [[nodiscard]] auto all_polygons() const -> std::vector<Polygon> {
        auto result = polygons_;
        if (front_) {
            auto polygons = front_->all_polygons();
            result.insert(result.end(), std::make_move_iterator(polygons.begin()),
                          std::make_move_iterator(polygons.end()));
        }
        if (back_) {
            auto polygons = back_->all_polygons();
            result.insert(result.end(), std::make_move_iterator(polygons.begin()),
                          std::make_move_iterator(polygons.end()));
        }
        return result;
    }

    auto build(const std::vector<Polygon>& source) -> void {
        if (source.empty())
            return;
        if (!plane_)
            plane_ = source.front().plane;
        std::vector<Polygon> front;
        std::vector<Polygon> back;
        for (const auto& polygon : source)
            plane_->split(polygon, polygons_, polygons_, front, back);
        if (!front.empty()) {
            if (!front_)
                front_ = std::make_unique<Node>();
            front_->build(front);
        }
        if (!back.empty()) {
            if (!back_)
                back_ = std::make_unique<Node>();
            back_->build(back);
        }
    }

  private:
    std::optional<Plane> plane_;
    std::vector<Polygon> polygons_;
    std::unique_ptr<Node> front_;
    std::unique_ptr<Node> back_;
};

[[nodiscard]] auto polygons_from(const Part& part) -> std::vector<Polygon> {
    std::vector<Polygon> polygons;
    polygons.reserve(part.triangles.size());
    for (const auto& triangle : part.triangles) {
        Polygon polygon{{Vertex{part.vertices[triangle[0]]}, Vertex{part.vertices[triangle[1]]},
                         Vertex{part.vertices[triangle[2]]}}};
        if (magnitude(polygon.plane.normal) > epsilon)
            polygons.push_back(std::move(polygon));
    }
    return polygons;
}

[[nodiscard]] auto combine(const std::vector<Polygon>& first,
                           const std::vector<Polygon>& second,
                           compiler::ir::FeatureOperation operation) -> std::vector<Polygon> {
    Node first_tree{first};
    Node second_tree{second};
    if (operation == compiler::ir::FeatureOperation::unite) {
        first_tree.clip_to(second_tree);
        second_tree.clip_to(first_tree);
        second_tree.invert();
        second_tree.clip_to(first_tree);
        second_tree.invert();
        first_tree.build(second_tree.all_polygons());
        return first_tree.all_polygons();
    }
    if (operation == compiler::ir::FeatureOperation::cut) {
        first_tree.invert();
        first_tree.clip_to(second_tree);
        second_tree.clip_to(first_tree);
        second_tree.invert();
        second_tree.clip_to(first_tree);
        second_tree.invert();
        first_tree.build(second_tree.all_polygons());
        first_tree.invert();
        return first_tree.all_polygons();
    }
    first_tree.invert();
    second_tree.clip_to(first_tree);
    second_tree.invert();
    first_tree.clip_to(second_tree);
    second_tree.clip_to(first_tree);
    first_tree.build(second_tree.all_polygons());
    first_tree.invert();
    return first_tree.all_polygons();
}

struct QuantizedPoint {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t z{};

    auto operator<=>(const QuantizedPoint&) const = default;
};

[[nodiscard]] auto quantized(const Point3& point) -> QuantizedPoint {
    return {static_cast<std::int64_t>(std::llround(point.x / epsilon)),
            static_cast<std::int64_t>(std::llround(point.y / epsilon)),
            static_cast<std::int64_t>(std::llround(point.z / epsilon))};
}

[[nodiscard]] auto reconstructed(const std::vector<Polygon>& polygons, std::string name,
                                 std::vector<std::string>& repairs) -> Part {
    Part part;
    part.name = std::move(name);
    std::map<QuantizedPoint, std::size_t> vertices;
    std::set<std::array<std::size_t, 3>> triangles;
    const auto vertex_index = [&](const Point3& point) {
        const auto key = quantized(point);
        const auto found = vertices.find(key);
        if (found != vertices.end())
            return found->second;
        const auto index = part.vertices.size();
        part.vertices.push_back(point);
        vertices.emplace(key, index);
        return index;
    };
    // Weld every BSP vertex before triangulation.  Split polygons can meet a
    // neighbour at a point in the middle of one of its edges; collecting the
    // complete point set lets the reconstruction make those T-junctions
    // conforming instead of emitting an open faceted shell.
    for (const auto& polygon : polygons) {
        for (const auto& vertex : polygon.vertices)
            static_cast<void>(vertex_index(vertex.position));
    }
    const std::size_t boundary_vertex_count = part.vertices.size();

    std::size_t conforming_split_count = 0;
    std::size_t degenerate_count = 0;
    std::size_t duplicate_count = 0;
    for (const auto& polygon : polygons) {
        if (polygon.vertices.size() < 3)
            continue;

        std::vector<std::size_t> boundary;
        for (std::size_t edge = 0; edge < polygon.vertices.size(); ++edge) {
            const Point3& start = polygon.vertices[edge].position;
            const Point3& finish = polygon.vertices[(edge + 1) % polygon.vertices.size()].position;
            const Vector3 direction = subtract(finish, start);
            const double length_squared = dot(direction, direction);
            if (length_squared <= epsilon * epsilon)
                continue;

            const auto start_index = vertex_index(start);
            if (boundary.empty() || boundary.back() != start_index)
                boundary.push_back(start_index);

            std::vector<std::pair<double, std::size_t>> points_on_edge;
            for (std::size_t candidate = 0; candidate < boundary_vertex_count; ++candidate) {
                if (candidate == start_index)
                    continue;
                const Vector3 offset = subtract(part.vertices[candidate], start);
                const double amount = dot(offset, direction) / length_squared;
                if (amount <= epsilon || amount >= 1.0 - epsilon)
                    continue;
                const Point3 projected = add(start, scale(direction, amount));
                if (magnitude(subtract(part.vertices[candidate], projected)) <= epsilon * 8.0)
                    points_on_edge.emplace_back(amount, candidate);
            }
            std::ranges::sort(points_on_edge);
            for (const auto& [amount, candidate] : points_on_edge) {
                static_cast<void>(amount);
                if (boundary.back() != candidate) {
                    boundary.push_back(candidate);
                    ++conforming_split_count;
                }
            }
        }
        if (boundary.size() < 3)
            continue;
        if (boundary.front() == boundary.back())
            boundary.pop_back();

        Point3 center{};
        for (const auto index : boundary)
            center = add(center, scale(part.vertices[index], 1.0 / static_cast<double>(boundary.size())));
        const auto center_index = vertex_index(center);
        for (std::size_t index = 0; index < boundary.size(); ++index) {
            const Triangle triangle{center_index, boundary[index],
                                    boundary[(index + 1) % boundary.size()]};
            if (triangle[0] == triangle[1] || triangle[1] == triangle[2] ||
                triangle[2] == triangle[0] ||
                magnitude(cross(subtract(part.vertices[triangle[1]], part.vertices[triangle[0]]),
                                subtract(part.vertices[triangle[2]], part.vertices[triangle[0]]))) <=
                    epsilon) {
                ++degenerate_count;
                continue;
            }
            auto canonical = triangle;
            std::ranges::sort(canonical);
            if (!triangles.insert(canonical).second) {
                ++duplicate_count;
                continue;
            }
            part.triangles.push_back(triangle);
        }
    }
    if (conforming_split_count != 0)
        repairs.push_back("inserted " + std::to_string(conforming_split_count) +
                          " conforming boolean boundary splits");
    if (degenerate_count != 0)
        repairs.push_back("removed " + std::to_string(degenerate_count) +
                          " degenerate boolean triangles");
    if (duplicate_count != 0)
        repairs.push_back("removed " + std::to_string(duplicate_count) +
                          " duplicate boolean triangles");
    return part;
}

} // namespace

auto apply_boolean(const Part& first, const Part& second,
                   compiler::ir::FeatureOperation operation, std::string result_name)
    -> BooleanResult {
    BooleanResult result;
    result.part = reconstructed(combine(polygons_from(first), polygons_from(second), operation),
                                std::move(result_name), result.repairs);
    result.part.body = first.body;
    result.part.material = first.material;
    return result;
}

} // namespace icad::cad
