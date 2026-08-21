#include "icad/ai/inspector.hpp"

#include "icad/cad/analysis.hpp"
#include "icad/cad/intersection.hpp"
#include "icad/cad/topology.hpp"
#include "icad/compiler/dependency_graph.hpp"
#include "icad/constraints/validator.hpp"
#include "icad/document/revision.hpp"
#include "icad/manufacturing/validator.hpp"

#include "../cad/model.hpp"

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace icad::ai {
namespace {

[[nodiscard]] auto quoted(std::string_view value) -> std::string {
    std::string result{"\""};
    for (const char character : value) {
        switch (character) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(character);
            break;
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] auto feature_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += body.features.size();
    }
    return count;
}

[[nodiscard]] auto boolean_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.operation != compiler::ir::FeatureOperation::create;
        }));
    }
    return count;
}

[[nodiscard]] auto modeling_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.type == "CHAMFER" || feature.type == "FILLET" ||
                   feature.type == "LINEAR_PATTERN" || feature.type == "MIRROR";
        }));
    }
    return count;
}

[[nodiscard]] auto surface_operation_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& body : project.bodies) {
        count += static_cast<std::size_t>(std::ranges::count_if(body.features, [](const auto& feature) {
            return feature.type == "SWEEP" || feature.type == "LOFT" ||
                   feature.type == "FREEFORM" || feature.type == "REVOLVE";
        }));
    }
    return count;
}

[[nodiscard]] auto profile_segment_count(const compiler::ir::Project& project) -> std::size_t {
    std::size_t count = 0;
    for (const auto& profile : project.profiles)
        count += profile.segments.size();
    return count;
}

[[nodiscard]] auto curved_profile_segment_count(const compiler::ir::Project& project)
    -> std::size_t {
    std::size_t count = 0;
    for (const auto& profile : project.profiles) {
        count += static_cast<std::size_t>(
            std::ranges::count(profile.segments, compiler::ir::ProfileSegmentKind::circular_arc,
                               &compiler::ir::ProfileSegment::kind));
    }
    return count;
}

[[nodiscard]] auto severity_name(compiler::DiagnosticSeverity severity) -> std::string_view {
    switch (severity) {
    case compiler::DiagnosticSeverity::error:
        return "error";
    case compiler::DiagnosticSeverity::warning:
        return "warning";
    case compiler::DiagnosticSeverity::note:
        return "note";
    }
    return "error";
}

[[nodiscard]] auto joint_kind_name(compiler::ir::JointKind kind) -> std::string_view {
    switch (kind) {
    case compiler::ir::JointKind::fixed:
        return "fixed";
    case compiler::ir::JointKind::revolute:
        return "revolute";
    case compiler::ir::JointKind::prismatic:
        return "prismatic";
    }
    return "fixed";
}

[[nodiscard]] auto point_kind_name(compiler::ir::SpatialPointKind kind) -> std::string_view {
    return kind == compiler::ir::SpatialPointKind::offset ? "offset" : "absolute";
}

[[nodiscard]] auto direction_kind_name(compiler::ir::DirectionKind kind) -> std::string_view {
    switch (kind) {
    case compiler::ir::DirectionKind::components:
        return "components";
    case compiler::ir::DirectionKind::between_points:
        return "betweenPoints";
    case compiler::ir::DirectionKind::rotated:
        return "rotated";
    }
    return "components";
}

[[nodiscard]] auto sketch_status_name(compiler::ir::SketchSolveStatus status) -> std::string_view {
    switch (status) {
    case compiler::ir::SketchSolveStatus::fully_constrained:
        return "fullyConstrained";
    case compiler::ir::SketchSolveStatus::under_constrained:
        return "underConstrained";
    case compiler::ir::SketchSolveStatus::inconsistent:
        return "inconsistent";
    }
    return "inconsistent";
}

} // namespace

auto project_json(const compiler::ir::Project& project) -> std::string {
    const auto metrics = cad::analyze(project);
    const auto delivery_model = cad::build_model(project);
    const auto intersections =
        cad::analyze_intersections(project, project.tolerance.linear_mm);
    const auto topology = cad::build_topology(project);
    const auto topology_validation = cad::validate_topology(topology);
    const auto constraint_results = constraints::validate(project);
    const auto manufacturing_report = manufacturing::validate(project);
    const auto dependencies = compiler::build_dependency_graph(project);
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema\":\"icad.inspect.v1\",\"project\":" << quoted(project.name)
           << ",\"revision\":" << quoted(document::revision_id(document::fingerprint(project)))
           << ",\"units\":" << quoted(project.canonical_length_unit)
           << ",\"tolerance\":{\"linearMm\":" << project.tolerance.linear_mm
           << ",\"angularDeg\":" << project.tolerance.angular_degrees << "}"
           << ",\"counts\":{\"parameters\":" << project.parameters.size()
           << ",\"angles\":" << project.angles.size() << ",\"points\":" << project.points.size()
           << ",\"vectors\":" << project.vectors.size() << ",\"poses\":" << project.poses.size()
           << ",\"instances\":" << project.instances.size()
           << ",\"joints\":" << project.joints.size()
           << ",\"materials\":" << project.materials.size()
           << ",\"profiles\":" << project.profiles.size()
           << ",\"sketches\":" << project.sketches.size()
           << ",\"profileSegments\":" << profile_segment_count(project)
           << ",\"curvedProfileSegments\":" << curved_profile_segment_count(project)
           << ",\"bodies\":" << project.bodies.size() << ",\"features\":" << feature_count(project)
           << ",\"booleanOperations\":" << boolean_operation_count(project)
           << ",\"modelingOperations\":" << modeling_operation_count(project)
           << ",\"surfaceOperations\":" << surface_operation_count(project)
           << ",\"dependencyNodes\":" << dependencies.nodes.size()
           << ",\"constraints\":" << project.constraints.size()
           << ",\"mates\":" << project.mates.size()
           << ",\"scenes\":" << project.scenes.size()
           << "},\"metrics\":{\"surfaceAreaMm2\":" << metrics.surface_area_mm2
           << ",\"volumeMm3\":" << metrics.volume_mm3 << ",\"boundsMin\":["
           << metrics.bounds.minimum[0] << ',' << metrics.bounds.minimum[1] << ','
           << metrics.bounds.minimum[2] << "],\"boundsMax\":[" << metrics.bounds.maximum[0] << ','
           << metrics.bounds.maximum[1] << ',' << metrics.bounds.maximum[2]
           << "]},\"topology\":{\"valid\":" << (topology_validation.valid() ? "true" : "false")
           << ",\"solids\":" << topology.solids.size()
           << ",\"vertices\":" << topology.vertex_count() << ",\"edges\":" << topology.edge_count()
           << ",\"wires\":" << topology.wire_count() << ",\"faces\":" << topology.face_count()
           << "},\"dependencies\":{\"edges\":" << dependencies.edge_count
           << ",\"evaluationOrder\":[";
    for (std::size_t index = 0; index < dependencies.evaluation_order.size(); ++index) {
        if (index != 0)
            output << ',';
        output << quoted(dependencies.evaluation_order[index]);
    }
    output << "],\"nodes\":[";
    for (std::size_t index = 0; index < dependencies.nodes.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& node = dependencies.nodes[index];
        output << "{\"id\":" << quoted(node.id) << ",\"kind\":" << quoted(node.kind)
               << ",\"dependsOn\":[";
        for (std::size_t dependency = 0; dependency < node.dependencies.size(); ++dependency) {
            if (dependency != 0)
                output << ',';
            output << quoted(node.dependencies[dependency]);
        }
        output << "]}";
    }
    output << "]},\"modeling\":{\"operations\":[";
    bool first_operation = true;
    for (const auto& body : project.bodies) {
        for (const auto& feature : body.features) {
            if (feature.type != "CHAMFER" && feature.type != "FILLET" &&
                feature.type != "LINEAR_PATTERN" && feature.type != "MIRROR")
                continue;
            if (!first_operation)
                output << ',';
            first_operation = false;
            output << "{\"body\":" << quoted(body.name) << ",\"feature\":"
                   << quoted(feature.name) << ",\"type\":" << quoted(feature.type);
            if (!feature.selected_edge_point.empty())
                output << ",\"edgeNearestPoint\":" << quoted(feature.selected_edge_point);
            if (!feature.direction.empty())
                output << ",\"direction\":" << quoted(feature.direction)
                       << ",\"count\":" << feature.count;
            if (!feature.plane_point.empty())
                output << ",\"planePoint\":" << quoted(feature.plane_point)
                       << ",\"planeNormal\":" << quoted(feature.plane_normal);
            output << '}';
        }
    }
    output << "],\"surfaces\":[";
    bool first_surface = true;
    for (const auto& body : project.bodies) {
        for (const auto& feature : body.features) {
            if (feature.type != "SWEEP" && feature.type != "LOFT" &&
                feature.type != "FREEFORM" && feature.type != "REVOLVE")
                continue;
            if (!first_surface)
                output << ',';
            first_surface = false;
            output << "{\"body\":" << quoted(body.name) << ",\"feature\":"
                   << quoted(feature.name) << ",\"type\":" << quoted(feature.type)
                   << ",\"profile\":" << quoted(feature.profile);
            if (!feature.target_profile.empty())
                output << ",\"targetProfile\":" << quoted(feature.target_profile);
            if (!feature.path_points.empty()) {
                output << ",\"path\":[";
                for (std::size_t point = 0; point < feature.path_points.size(); ++point) {
                    if (point != 0)
                        output << ',';
                    output << quoted(feature.path_points[point]);
                }
                output << ']';
            }
            if (feature.type == "FREEFORM")
                output << ",\"sections\":" << feature.count;
            output << '}';
        }
    }
    output << "],\"repairs\":[";
    bool first_repair = true;
    for (const auto& part : delivery_model.parts) {
        if (!part.boolean_result && !part.faceted_result)
            continue;
        if (!first_repair)
            output << ',';
        first_repair = false;
        output << "{\"body\":" << quoted(part.body) << ",\"result\":" << quoted(part.name)
               << ",\"actions\":[";
        for (std::size_t action = 0; action < part.repairs.size(); ++action) {
            if (action != 0)
                output << ',';
            output << quoted(part.repairs[action]);
        }
        output << "]}";
    }
    output << "]},\"intersections\":{\"partPairCandidates\":"
           << intersections.part_pair_candidates
           << ",\"trianglePairCandidates\":" << intersections.triangle_pair_candidates
           << ",\"intersectingPartPairs\":" << intersections.intersecting_part_pairs
           << ",\"intersectingTrianglePairs\":" << intersections.intersecting_triangle_pairs
           << ",\"penetratingPartPairs\":" << intersections.penetrating_part_pairs
           << ",\"containedPartPairs\":" << intersections.contained_part_pairs
           << ",\"surfaceContactOnlyPartPairs\":"
           << intersections.surface_contact_only_part_pairs
           << ",\"bodyContacts\":[";
    for (std::size_t index = 0; index < intersections.body_contacts.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& contact = intersections.body_contacts[index];
        output << "{\"firstBody\":" << quoted(contact.first_body)
               << ",\"secondBody\":" << quoted(contact.second_body)
               << ",\"partPairCandidates\":" << contact.part_pair_candidates
               << ",\"trianglePairCandidates\":" << contact.triangle_pair_candidates
               << ",\"intersectingPartPairs\":" << contact.intersecting_part_pairs
               << ",\"intersectingTrianglePairs\":" << contact.intersecting_triangle_pairs
               << ",\"penetratingPartPairs\":" << contact.penetrating_part_pairs
               << ",\"containedPartPairs\":" << contact.contained_part_pairs
               << ",\"surfaceContactOnlyPartPairs\":"
               << contact.surface_contact_only_part_pairs
               << '}';
    }
    output << "]},\"spatial\":{\"angles\":[";
    for (std::size_t index = 0; index < project.angles.size(); ++index) {
        if (index != 0)
            output << ',';
        output << "{\"name\":" << quoted(project.angles[index].name)
               << ",\"degrees\":" << project.angles[index].degrees << '}';
    }
    output << "],\"points\":[";
    for (std::size_t index = 0; index < project.points.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& point = project.points[index];
        output << "{\"name\":" << quoted(point.name)
               << ",\"kind\":" << quoted(point_kind_name(point.kind)) << ",\"positionMm\":["
               << point.position_mm[0] << ',' << point.position_mm[1] << ','
               << point.position_mm[2] << ']';
        if (point.kind == compiler::ir::SpatialPointKind::offset) {
            output << ",\"from\":" << quoted(point.base_point)
                   << ",\"along\":" << quoted(point.direction)
                   << ",\"distanceMm\":" << point.distance_mm;
            if (!point.distance_reference.empty())
                output << ",\"distanceReference\":" << quoted(point.distance_reference);
        }
        output << '}';
    }
    output << "],\"vectors\":[";
    for (std::size_t index = 0; index < project.vectors.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& vector = project.vectors[index];
        output << "{\"name\":" << quoted(vector.name)
               << ",\"kind\":" << quoted(direction_kind_name(vector.kind)) << ",\"unit\":["
               << vector.unit[0] << ',' << vector.unit[1] << ',' << vector.unit[2] << ']';
        if (vector.kind == compiler::ir::DirectionKind::between_points) {
            output << ",\"from\":" << quoted(vector.from_point)
                   << ",\"to\":" << quoted(vector.to_point);
        } else if (vector.kind == compiler::ir::DirectionKind::rotated) {
            output << ",\"source\":" << quoted(vector.source_direction)
                   << ",\"around\":" << quoted(vector.around_axis)
                   << ",\"angleDeg\":" << vector.angle_degrees;
            if (!vector.angle_reference.empty())
                output << ",\"angleReference\":" << quoted(vector.angle_reference);
        }
        output << '}';
    }
    output << "]},\"sketches\":[";
    for (std::size_t sketch_index = 0; sketch_index < project.sketches.size(); ++sketch_index) {
        if (sketch_index != 0)
            output << ',';
        const auto& sketch = project.sketches[sketch_index];
        output << "{\"name\":" << quoted(sketch.name)
               << ",\"status\":" << quoted(sketch_status_name(sketch.status))
               << ",\"degreesOfFreedom\":" << sketch.degrees_of_freedom
               << ",\"iterations\":" << sketch.iterations
               << ",\"maximumResidual\":" << sketch.maximum_residual << ",\"points\":[";
        for (std::size_t point_index = 0; point_index < sketch.points.size(); ++point_index) {
            if (point_index != 0)
                output << ',';
            const auto& point_value = sketch.points[point_index];
            output << "{\"name\":" << quoted(point_value.name)
                   << ",\"fixed\":" << (point_value.fixed ? "true" : "false")
                   << ",\"initialMm\":[" << point_value.initial.x_mm << ','
                   << point_value.initial.y_mm << "],\"solvedMm\":[" << point_value.solved.x_mm
                   << ',' << point_value.solved.y_mm << "]}";
        }
        output << "],\"constraints\":[";
        for (std::size_t constraint_index = 0;
             constraint_index < sketch.constraints.size(); ++constraint_index) {
            if (constraint_index != 0)
                output << ',';
            const auto& constraint = sketch.constraints[constraint_index];
            output << "{\"name\":" << quoted(constraint.name)
                   << ",\"type\":" << quoted(constraint.kind) << ",\"references\":[";
            for (std::size_t reference = 0; reference < constraint.references.size(); ++reference) {
                if (reference != 0)
                    output << ',';
                output << quoted(constraint.references[reference]);
            }
            output << ']';
            if (!constraint.target_unit.empty()) {
                output << ",\"target\":" << constraint.target_value
                       << ",\"unit\":" << quoted(constraint.target_unit);
            }
            if (!constraint.target_reference.empty())
                output << ",\"targetReference\":" << quoted(constraint.target_reference);
            output << '}';
        }
        output << "]}";
    }
    output << "],\"mechanism\":{\"degreesOfFreedom\":"
           << std::ranges::count_if(
                  project.joints,
                  [](const auto& joint) { return joint.kind != compiler::ir::JointKind::fixed; })
           << ",\"poses\":[";
    for (std::size_t index = 0; index < project.poses.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& pose = project.poses[index];
        output << "{\"body\":" << quoted(pose.body) << ",\"at\":" << quoted(pose.point)
               << ",\"positionMm\":[" << pose.transform.position_mm[0] << ','
               << pose.transform.position_mm[1] << ',' << pose.transform.position_mm[2]
               << "],\"rotationDeg\":[" << pose.transform.rotation_deg[0] << ','
               << pose.transform.rotation_deg[1] << ',' << pose.transform.rotation_deg[2] << "]}";
    }
    output << "],\"instances\":[";
    for (std::size_t index = 0; index < project.instances.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& instance = project.instances[index];
        output << "{\"name\":" << quoted(instance.name) << ",\"body\":"
               << quoted(instance.body) << ",\"at\":" << quoted(instance.point)
               << ",\"positionMm\":[" << instance.transform.position_mm[0] << ','
               << instance.transform.position_mm[1] << ',' << instance.transform.position_mm[2]
               << "],\"rotationDeg\":[" << instance.transform.rotation_deg[0] << ','
               << instance.transform.rotation_deg[1] << ',' << instance.transform.rotation_deg[2]
               << ']';
        const bool driven = std::ranges::any_of(
            project.joints,
            [&](const auto& joint) { return joint.child_body == instance.name && joint.driven; });
        output << ",\"jointDriven\":" << (driven ? "true" : "false");
        const auto solved_part =
            std::ranges::find(metrics.parts, instance.name, &cad::PartAnalysis::body);
        if (solved_part != metrics.parts.end()) {
            auto bounds = solved_part->bounds;
            for (auto part = std::next(solved_part); part != metrics.parts.end(); ++part) {
                if (part->body != instance.name)
                    continue;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], part->bounds.minimum[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], part->bounds.maximum[axis]);
                }
            }
            output << ",\"solvedBoundsMin\":[" << bounds.minimum[0] << ',' << bounds.minimum[1]
                   << ',' << bounds.minimum[2] << "],\"solvedBoundsMax\":[" << bounds.maximum[0]
                   << ',' << bounds.maximum[1] << ',' << bounds.maximum[2] << ']';
        }
        output << '}';
    }
    output << "],\"joints\":[";
    for (std::size_t index = 0; index < project.joints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& joint = project.joints[index];
        output << "{\"name\":" << quoted(joint.name)
               << ",\"type\":" << quoted(joint_kind_name(joint.kind))
               << ",\"parent\":" << quoted(joint.parent_body)
               << ",\"child\":" << quoted(joint.child_body) << ",\"at\":" << quoted(joint.point)
               << ",\"axis\":" << quoted(joint.axis) << ",\"value\":" << joint.value
               << ",\"limits\":[" << joint.lower_limit << ',' << joint.upper_limit
               << "],\"unit\":" << quoted(joint.unit);
        if (!joint.value_reference.empty())
            output << ",\"valueReference\":" << quoted(joint.value_reference);
        if (!joint.lower_limit_reference.empty())
            output << ",\"lowerLimitReference\":" << quoted(joint.lower_limit_reference);
        if (!joint.upper_limit_reference.empty())
            output << ",\"upperLimitReference\":" << quoted(joint.upper_limit_reference);
        output << '}';
    }
    output << "]},\"bodies\":[";
    for (std::size_t index = 0; index < project.bodies.size(); ++index) {
        const auto& body = project.bodies[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"name\":" << quoted(body.name) << ",\"material\":" << quoted(body.material)
               << ",\"features\":" << body.features.size();
        const auto first_part =
            std::ranges::find(metrics.parts, body.name, &cad::PartAnalysis::body);
        if (first_part != metrics.parts.end()) {
            auto bounds = first_part->bounds;
            for (auto part = std::next(first_part); part != metrics.parts.end(); ++part) {
                if (part->body != body.name)
                    continue;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], part->bounds.minimum[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], part->bounds.maximum[axis]);
                }
            }
            output << ",\"geometry\":{\"boundsMin\":[" << bounds.minimum[0] << ','
                   << bounds.minimum[1] << ',' << bounds.minimum[2] << "],\"boundsMax\":["
                   << bounds.maximum[0] << ',' << bounds.maximum[1] << ',' << bounds.maximum[2]
                   << "],\"centerMm\":[" << (bounds.minimum[0] + bounds.maximum[0]) * 0.5 << ','
                   << (bounds.minimum[1] + bounds.maximum[1]) * 0.5 << ','
                   << (bounds.minimum[2] + bounds.maximum[2]) * 0.5 << "],\"sizeMm\":["
                   << bounds.maximum[0] - bounds.minimum[0] << ','
                   << bounds.maximum[1] - bounds.minimum[1] << ','
                   << bounds.maximum[2] - bounds.minimum[2] << "]}";
        }
        output << '}';
    }
    output << "],\"constraints\":[";
    for (std::size_t index = 0; index < project.constraints.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& constraint = project.constraints[index];
        const auto& validation = constraint_results[index];
        output << "{\"name\":" << quoted(constraint.name) << ",\"type\":" << quoted(constraint.kind)
               << ",\"first\":" << quoted(constraint.first_body)
               << ",\"second\":" << quoted(constraint.second_body)
               << ",\"passed\":" << (validation.passed ? "true" : "false")
               << ",\"required\":" << validation.required_mm
               << ",\"actual\":" << validation.actual_mm
               << ",\"unit\":" << quoted(validation.unit);
        if (!constraint.target_reference.empty())
            output << ",\"targetReference\":" << quoted(constraint.target_reference);
        output << '}';
    }
    output << "],\"mates\":[";
    for (std::size_t index = 0; index < project.mates.size(); ++index) {
        if (index != 0)
            output << ',';
        const auto& mate = project.mates[index];
        const auto& validation = constraint_results[project.constraints.size() + index];
        output << "{\"name\":" << quoted(mate.name)
               << ",\"type\":"
               << quoted(mate.kind == compiler::ir::MateKind::face ? "FACE" : "EDGE")
               << ",\"firstOccurrence\":" << quoted(mate.first_occurrence)
               << ",\"firstSelector\":" << quoted(mate.first_selector)
               << ",\"secondOccurrence\":" << quoted(mate.second_occurrence)
               << ",\"secondSelector\":" << quoted(mate.second_selector)
               << ",\"passed\":" << (validation.passed ? "true" : "false")
               << ",\"requiredMm\":" << validation.required_mm
               << ",\"actualMm\":" << validation.actual_mm;
        if (!mate.target_reference.empty())
            output << ",\"targetReference\":" << quoted(mate.target_reference);
        output << '}';
    }
    output << "],\"scenes\":[";
    for (std::size_t scene_index = 0; scene_index < project.scenes.size(); ++scene_index) {
        if (scene_index != 0)
            output << ',';
        const auto& scene = project.scenes[scene_index];
        output << "{\"name\":" << quoted(scene.name)
               << ",\"durationSeconds\":" << scene.duration_seconds
               << ",\"fps\":" << scene.frames_per_second
               << ",\"background\":" << quoted(scene.background)
               << ",\"loopCount\":" << scene.loop_count
               << ",\"lights\":" << scene.lights.size()
               << ",\"events\":" << scene.events.size() << ",\"tracks\":[";
        for (std::size_t track_index = 0; track_index < scene.tracks.size(); ++track_index) {
            if (track_index != 0)
                output << ',';
            const auto& track = scene.tracks[track_index];
            output << "{\"name\":" << quoted(track.name)
                   << ",\"targetKind\":" << quoted(track.target_kind)
                   << ",\"target\":" << quoted(track.target)
                   << ",\"easing\":" << quoted(track.easing)
                   << ",\"keyframes\":" << track.keyframes.size();
            if (track.target_kind == "JOINT" && !track.keyframes.empty()) {
                const auto [minimum, maximum] = std::ranges::minmax_element(
                    track.keyframes, {}, &compiler::ir::Keyframe::joint_value);
                output << ",\"valueRange\":[" << minimum->joint_value << ','
                       << maximum->joint_value << "],\"unit\":"
                       << quoted(track.keyframes.front().joint_unit);
            }
            output << '}';
        }
        output << "]}";
    }
    output << "],\"validation\":{\"constraintsPassed\":"
           << (constraints::all_passed(constraint_results) ? "true" : "false")
           << ",\"manufacturingPassed\":" << (manufacturing_report.passed ? "true" : "false")
           << ",\"manufacturingIssues\":" << manufacturing_report.issues.size() << "}}";
    return output.str();
}

auto topology_json(const compiler::ir::Project& project) -> std::string {
    const auto topology = cad::build_topology(project);
    const auto validation = cad::validate_topology(topology);
    std::ostringstream output;
    output << std::setprecision(17) << "{\"schema\":\"icad.topology.v1\",\"valid\":"
           << (validation.valid() ? "true" : "false")
           << ",\"counts\":{\"solids\":" << topology.solids.size()
           << ",\"vertices\":" << topology.vertex_count() << ",\"edges\":" << topology.edge_count()
           << ",\"wires\":" << topology.wire_count() << ",\"faces\":" << topology.face_count()
           << "},\"solids\":[";
    for (std::size_t solid_index = 0; solid_index < topology.solids.size(); ++solid_index) {
        const auto& solid = topology.solids[solid_index];
        if (solid_index != 0)
            output << ',';
        const auto characteristic = solid.euler_characteristic();
        output << "{\"id\":" << quoted(solid.id) << ",\"body\":" << quoted(solid.body)
               << ",\"feature\":" << quoted(solid.feature)
               << ",\"featureType\":" << quoted(solid.feature_type)
               << ",\"shell\":" << quoted(solid.shell.id)
               << ",\"eulerCharacteristic\":" << characteristic
               << ",\"genus\":" << (2 - characteristic) / 2 << ",\"vertices\":[";
        for (std::size_t index = 0; index < solid.vertices.size(); ++index) {
            const auto& vertex = solid.vertices[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(vertex.id) << ",\"pointMm\":[" << vertex.point.x << ','
                   << vertex.point.y << ',' << vertex.point.z << "]}";
        }
        output << "],\"edges\":[";
        for (std::size_t index = 0; index < solid.edges.size(); ++index) {
            const auto& edge = solid.edges[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(edge.id)
                   << ",\"curve\":" << quoted(cad::curve_kind_name(edge.curve.kind))
                   << ",\"startVertex\":" << quoted(edge.start_vertex)
                   << ",\"endVertex\":" << quoted(edge.end_vertex)
                   << ",\"closed\":" << (edge.closed ? "true" : "false") << ",\"originMm\":["
                   << edge.curve.origin.x << ',' << edge.curve.origin.y << ','
                   << edge.curve.origin.z << "],\"direction\":[" << edge.curve.direction.x << ','
                   << edge.curve.direction.y << ',' << edge.curve.direction.z << "],\"axis\":["
                   << edge.curve.axis.x << ',' << edge.curve.axis.y << ',' << edge.curve.axis.z
                   << "],\"radiusMm\":" << edge.curve.radius << ",\"parameterRange\":["
                   << edge.curve.parameter_start << ',' << edge.curve.parameter_end << "]}";
        }
        output << "],\"faces\":[";
        for (std::size_t index = 0; index < solid.faces.size(); ++index) {
            const auto& face = solid.faces[index];
            if (index != 0)
                output << ',';
            output << "{\"id\":" << quoted(face.id)
                   << ",\"surface\":" << quoted(cad::surface_kind_name(face.surface.kind))
                   << ",\"originMm\":[" << face.surface.origin.x << ',' << face.surface.origin.y
                   << ',' << face.surface.origin.z << "],\"axis\":[" << face.surface.axis.x << ','
                   << face.surface.axis.y << ',' << face.surface.axis.z
                   << "],\"radiusMm\":" << face.surface.radius
                   << ",\"semiAngleRadians\":" << face.surface.semi_angle_radians
                   << ",\"boundaryWires\":[";
            for (std::size_t wire_index = 0; wire_index < face.boundaries.size(); ++wire_index) {
                const auto& wire = face.boundaries[wire_index];
                if (wire_index != 0)
                    output << ',';
                output << "{\"id\":" << quoted(wire.id) << ",\"edges\":[";
                for (std::size_t use_index = 0; use_index < wire.edges.size(); ++use_index) {
                    const auto& use = wire.edges[use_index];
                    if (use_index != 0)
                        output << ',';
                    output << "{\"edge\":" << quoted(use.edge)
                           << ",\"reversed\":" << (use.reversed ? "true" : "false") << '}';
                }
                output << "]}";
            }
            output << "]}";
        }
        output << "]}";
    }
    output << "],\"issues\":[";
    for (std::size_t index = 0; index < validation.issues.size(); ++index) {
        const auto& issue = validation.issues[index];
        if (index != 0)
            output << ',';
        output << "{\"code\":" << quoted(issue.code) << ",\"entity\":" << quoted(issue.entity)
               << ",\"message\":" << quoted(issue.message) << '}';
    }
    output << "]}";
    return output.str();
}

auto diagnostics_json(std::string_view source) -> std::string {
    const auto result = compiler::compile(source);
    std::ostringstream output;
    output << "{\"schema\":\"icad.diagnostics.v1\",\"ok\":" << (result.ok() ? "true" : "false")
           << ",\"diagnostics\":[";
    for (std::size_t index = 0; index < result.diagnostics.size(); ++index) {
        const auto& diagnostic = result.diagnostics[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"severity\":" << quoted(severity_name(diagnostic.severity))
               << ",\"code\":" << quoted(diagnostic.code)
               << ",\"message\":" << quoted(diagnostic.message)
               << ",\"line\":" << diagnostic.location.line
               << ",\"column\":" << diagnostic.location.column << '}';
    }
    output << "]}";
    return output.str();
}

} // namespace icad::ai
