#include "icad/compiler/dependency_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace icad::compiler {
namespace {

auto append_unique(std::vector<std::string>& values, std::string value) -> void {
    if (!value.empty() && !std::ranges::contains(values, value))
        values.push_back(std::move(value));
}

auto add_node(DependencyGraph& graph, std::string id, std::string kind,
              std::vector<std::string> dependencies = {}) -> void {
    graph.edge_count += dependencies.size();
    graph.nodes.push_back({std::move(id), std::move(kind), std::move(dependencies)});
}

[[nodiscard]] auto has_sketch(const ir::Project& project, const std::string& name) -> bool {
    return std::ranges::any_of(project.sketches,
                               [&](const auto& sketch) { return sketch.name == name; });
}

} // namespace

auto build_dependency_graph(const ir::Project& project) -> DependencyGraph {
    DependencyGraph graph;
    add_node(graph, "tolerance:project", "tolerance");
    for (const auto& parameter : project.parameters)
        add_node(graph, "parameter:" + parameter.name, "parameter");
    for (const auto& angle : project.angles)
        add_node(graph, "angle:" + angle.name, "angle");
    for (const auto& material : project.materials)
        add_node(graph, "material:" + material.name, "material");
    for (const auto& point : project.points) {
        std::vector<std::string> dependencies;
        if (point.kind == ir::SpatialPointKind::offset) {
            append_unique(dependencies, "point:" + point.base_point);
            append_unique(dependencies, "vector:" + point.direction);
            if (!point.distance_reference.empty())
                append_unique(dependencies, "parameter:" + point.distance_reference);
        }
        add_node(graph, "point:" + point.name, "point", std::move(dependencies));
    }
    for (const auto& direction : project.vectors) {
        std::vector<std::string> dependencies;
        if (direction.kind == ir::DirectionKind::between_points) {
            append_unique(dependencies, "point:" + direction.from_point);
            append_unique(dependencies, "point:" + direction.to_point);
        } else if (direction.kind == ir::DirectionKind::rotated) {
            append_unique(dependencies, "vector:" + direction.source_direction);
            append_unique(dependencies, "vector:" + direction.around_axis);
            if (!direction.angle_reference.empty())
                append_unique(dependencies, "angle:" + direction.angle_reference);
        }
        add_node(graph, "vector:" + direction.name, "vector", std::move(dependencies));
    }
    for (const auto& sketch : project.sketches) {
        std::vector<std::string> dependencies;
        for (const auto& constraint : sketch.constraints) {
            if (!constraint.target_reference.empty()) {
                append_unique(dependencies,
                              (constraint.kind == "ANGLE" ? "angle:" : "parameter:") +
                                  constraint.target_reference);
            }
        }
        add_node(graph, "sketch:" + sketch.name, "sketch", std::move(dependencies));
    }
    for (const auto& profile : project.profiles) {
        std::vector<std::string> dependencies;
        if (has_sketch(project, profile.name))
            dependencies.push_back("sketch:" + profile.name);
        add_node(graph, "profile:" + profile.name, "profile", std::move(dependencies));
    }
    for (const auto& pose : project.poses)
        add_node(graph, "pose:" + pose.body, "pose", {"point:" + pose.point});
    for (const auto& instance : project.instances) {
        add_node(graph, "instance:" + instance.name, "instance",
                 {"body:" + instance.body, "point:" + instance.point});
    }

    for (const auto& body : project.bodies) {
        std::string previous_feature;
        std::vector<std::string> body_dependencies;
        for (const auto& feature : body.features) {
            std::vector<std::string> dependencies;
            if (!previous_feature.empty())
                append_unique(dependencies, previous_feature);
            if (!feature.profile.empty())
                append_unique(dependencies, "profile:" + feature.profile);
            if (!feature.target_profile.empty())
                append_unique(dependencies, "profile:" + feature.target_profile);
            if (!feature.selected_edge_point.empty())
                append_unique(dependencies, "point:" + feature.selected_edge_point);
            if (!feature.direction.empty())
                append_unique(dependencies, "vector:" + feature.direction);
            if (!feature.plane_point.empty())
                append_unique(dependencies, "point:" + feature.plane_point);
            if (!feature.plane_normal.empty())
                append_unique(dependencies, "vector:" + feature.plane_normal);
            for (const auto& path_point : feature.path_points)
                append_unique(dependencies, "point:" + path_point);
            const std::string feature_id = "feature:" + body.name + '/' + feature.name;
            add_node(graph, feature_id, "feature", std::move(dependencies));
            previous_feature = feature_id;
            body_dependencies.push_back(feature_id);
        }
        if (!body.material.empty())
            append_unique(body_dependencies, "material:" + body.material);
        if (std::ranges::any_of(project.poses,
                                [&](const auto& pose) { return pose.body == body.name; }))
            append_unique(body_dependencies, "pose:" + body.name);
        add_node(graph, "body:" + body.name, "body", std::move(body_dependencies));
    }
    for (const auto& constraint : project.constraints) {
        std::vector<std::string> dependencies;
        const std::string prefix = constraint.kind == "MIN_DISTANCE"
                                       ? "body:"
                                   : constraint.kind == "COINCIDENT" ? "point:" : "vector:";
        append_unique(dependencies, prefix + constraint.first_body);
        append_unique(dependencies, prefix + constraint.second_body);
        if (!constraint.target_reference.empty()) {
            append_unique(dependencies,
                          (constraint.kind == "ANGLE_BETWEEN" ? "angle:" : "parameter:") +
                              constraint.target_reference);
        }
        add_node(graph, "constraint:" + constraint.name, "constraint",
                 std::move(dependencies));
    }
    for (const auto& joint : project.joints) {
        std::vector<std::string> dependencies{
            joint.parent_body == "WORLD" ? "tolerance:project"
                                           : (std::ranges::any_of(project.instances, [&](const auto& instance) {
                                                  return instance.name == joint.parent_body;
                                              })
                                                  ? "instance:" + joint.parent_body
                                                  : "body:" + joint.parent_body),
            std::ranges::any_of(project.instances, [&](const auto& instance) {
                return instance.name == joint.child_body;
            })
                ? "instance:" + joint.child_body
                : "body:" + joint.child_body,
            "point:" + joint.point, "vector:" + joint.axis};
        add_node(graph, "joint:" + joint.name, "joint", std::move(dependencies));
    }
    for (const auto& mate : project.mates) {
        const auto occurrence_id = [&](const std::string& occurrence) {
            return std::ranges::any_of(project.instances, [&](const auto& instance) {
                       return instance.name == occurrence;
                   })
                       ? "instance:" + occurrence
                       : "body:" + occurrence;
        };
        std::vector<std::string> dependencies{occurrence_id(mate.first_occurrence),
                                              occurrence_id(mate.second_occurrence),
                                              "tolerance:project"};
        if (!mate.target_reference.empty())
            append_unique(dependencies, "parameter:" + mate.target_reference);
        add_node(graph, "mate:" + mate.name, "mate", std::move(dependencies));
    }
    for (const auto& scene : project.scenes) {
        std::vector<std::string> dependencies;
        for (const auto& track : scene.tracks) {
            if (track.target_kind == "BODY")
                append_unique(dependencies,
                              std::ranges::any_of(project.instances, [&](const auto& instance) {
                                  return instance.name == track.target;
                              })
                                  ? "instance:" + track.target
                                  : "body:" + track.target);
            else if (track.target_kind == "JOINT")
                append_unique(dependencies, "joint:" + track.target);
        }
        add_node(graph, "scene:" + scene.name, "scene", std::move(dependencies));
    }

    std::unordered_map<std::string, std::size_t> node_index;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
        node_index.emplace(graph.nodes[index].id, index);
    std::vector<std::size_t> indegree(graph.nodes.size(), 0);
    std::vector<std::vector<std::size_t>> consumers(graph.nodes.size());
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        for (const auto& dependency : graph.nodes[index].dependencies) {
            const auto found = node_index.find(dependency);
            if (found == node_index.end())
                continue;
            ++indegree[index];
            consumers[found->second].push_back(index);
        }
    }
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < indegree.size(); ++index) {
        if (indegree[index] == 0)
            ready.push_back(index);
    }
    std::size_t cursor = 0;
    while (cursor < ready.size()) {
        const std::size_t node = ready[cursor++];
        graph.evaluation_order.push_back(graph.nodes[node].id);
        for (const std::size_t consumer : consumers[node]) {
            if (--indegree[consumer] == 0)
                ready.push_back(consumer);
        }
    }
    return graph;
}

} // namespace icad::compiler
