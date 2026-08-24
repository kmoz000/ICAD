#include "icad/compiler/dependency_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
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

} // namespace

auto build_dependency_graph(const ir::Project& project) -> DependencyGraph {
    DependencyGraph graph;
    add_node(graph, "tolerance:project", "tolerance");
    const std::string project_prefix = project.name + ".";
    for (const auto& parameter : project.parameters) {
        std::vector<std::string> dependencies;
        for (const auto& reference : parameter.dependencies) {
            const std::string_view local = reference.starts_with(project_prefix)
                                               ? std::string_view{reference}.substr(
                                                     project_prefix.size())
                                               : std::string_view{reference};
            append_unique(dependencies, "parameter:" + std::string{local});
        }
        add_node(graph, "parameter:" + parameter.name, "parameter",
                 std::move(dependencies));
    }
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
    for (const auto& body : project.bodies) {
        for (const auto& reference : body.face_references) {
            add_node(graph, "face-reference:" + body.name + '/' + reference.name,
                     "face-reference",
                     {"feature:" + body.name + '/' + reference.feature});
        }
    }
    for (const auto& sketch : project.sketches) {
        std::vector<std::string> dependencies;
        if (!sketch.body.empty() && !sketch.support_feature.empty())
            append_unique(dependencies,
                          "feature:" + sketch.body + '/' + sketch.support_feature);
        if (!sketch.body.empty() && !sketch.support_reference.empty()) {
            const auto body = std::ranges::find(project.bodies, sketch.body,
                                                &ir::Body::name);
            if (body != project.bodies.end() &&
                std::ranges::any_of(body->face_references, [&](const auto& reference) {
                    return reference.name == sketch.support_reference;
                })) {
                append_unique(dependencies,
                              "face-reference:" + sketch.body + '/' +
                                  sketch.support_reference);
            }
        }
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
        const auto sketch = std::ranges::find_if(project.sketches, [&](const auto& candidate) {
            return candidate.name == profile.name ||
                   profile.name.starts_with(candidate.name + ".");
        });
        if (sketch != project.sketches.end())
            dependencies.push_back("sketch:" + sketch->name);
        add_node(graph, "profile:" + profile.name, "profile", std::move(dependencies));
    }
    for (const auto& sketch : project.sketches) {
        for (const auto& region : sketch.regions) {
            std::vector<std::string> dependencies{"profile:" + region.outer_profile};
            for (const auto& hole_profile : region.hole_profiles)
                append_unique(dependencies, "profile:" + hole_profile);
            add_node(graph, "region:" + sketch.name + "." + region.name, "region",
                     std::move(dependencies));
        }
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
        for (const auto& selection : body.topology_selections) {
            const std::string selection_id =
                "selection:" + body.name + '/' + selection.name;
            add_node(graph, selection_id, "topology_selection",
                     {"feature:" + body.name + '/' + selection.source_feature});
            body_dependencies.push_back(selection_id);
        }
        for (const auto& feature : body.features) {
            std::vector<std::string> dependencies;
            if (!previous_feature.empty())
                append_unique(dependencies, previous_feature);
            if (!feature.region.empty())
                append_unique(dependencies, "region:" + feature.region);
            else if (!feature.profile.empty())
                append_unique(dependencies, "profile:" + feature.profile);
            if (!feature.target_profile.empty())
                append_unique(dependencies, "profile:" + feature.target_profile);
            if (!feature.selected_edge_point.empty())
                append_unique(dependencies, "point:" + feature.selected_edge_point);
            if (!feature.selected_edge_set.empty())
                append_unique(dependencies,
                              "selection:" + body.name + '/' + feature.selected_edge_set);
            if (!feature.direction.empty())
                append_unique(dependencies, "vector:" + feature.direction);
            if (!feature.plane_point.empty())
                append_unique(dependencies, "point:" + feature.plane_point);
            if (!feature.plane_normal.empty())
                append_unique(dependencies, "vector:" + feature.plane_normal);
            if (!feature.support_feature.empty())
                append_unique(dependencies,
                              "feature:" + body.name + '/' + feature.support_feature);
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
