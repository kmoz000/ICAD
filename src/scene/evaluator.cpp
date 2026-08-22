#include "icad/scene/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <vector>

namespace icad::scene {
namespace {

auto rotate_axis(cad::Point3& point, const cad::Point3& pivot, const cad::Vector3& axis,
                 double degrees) -> void {
    const double radians = degrees * std::numbers::pi / 180.0;
    const cad::Point3 relative{point.x - pivot.x, point.y - pivot.y, point.z - pivot.z};
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const cad::Point3 crossed{axis.y * relative.z - axis.z * relative.y,
                              axis.z * relative.x - axis.x * relative.z,
                              axis.x * relative.y - axis.y * relative.x};
    const double projection =
        axis.x * relative.x + axis.y * relative.y + axis.z * relative.z;
    point = {pivot.x + relative.x * cosine + crossed.x * sine +
                 axis.x * projection * (1.0 - cosine),
             pivot.y + relative.y * cosine + crossed.y * sine +
                 axis.y * projection * (1.0 - cosine),
             pivot.z + relative.z * cosine + crossed.z * sine +
                 axis.z * projection * (1.0 - cosine)};
}

[[nodiscard]] auto eased(std::string_view easing, double amount) -> double {
    if (easing == "STEP")
        return 0.0;
    if (easing == "EASE_IN")
        return amount * amount;
    if (easing == "EASE_OUT")
        return 1.0 - (1.0 - amount) * (1.0 - amount);
    if (easing == "EASE_IN_OUT")
        return amount < 0.5 ? 2.0 * amount * amount
                            : 1.0 - std::pow(-2.0 * amount + 2.0, 2.0) / 2.0;
    return amount;
}

[[nodiscard]] auto sample_track(const compiler::ir::AnimationTrack& track,
                                double time_seconds) -> double {
    if (track.keyframes.empty())
        return 0.0;
    if (time_seconds <= track.keyframes.front().time_seconds)
        return track.keyframes.front().joint_value;
    if (time_seconds >= track.keyframes.back().time_seconds)
        return track.keyframes.back().joint_value;
    const auto right = std::ranges::find_if(track.keyframes, [&](const auto& frame) {
        return frame.time_seconds >= time_seconds;
    });
    const auto left = std::prev(right);
    const double span = right->time_seconds - left->time_seconds;
    const double amount = span <= 0.0 ? 0.0 : (time_seconds - left->time_seconds) / span;
    const double blend = eased(track.easing, amount);
    return left->joint_value + (right->joint_value - left->joint_value) * blend;
}

struct Operation {
    compiler::ir::JointKind kind{compiler::ir::JointKind::fixed};
    cad::Point3 pivot;
    cad::Vector3 axis;
    double delta{};
};

[[nodiscard]] auto joint_chain(const compiler::ir::Project& project, std::string_view body)
    -> std::vector<const compiler::ir::Joint*> {
    std::vector<const compiler::ir::Joint*> chain;
    std::string child{body};
    for (std::size_t guard = 0; guard <= project.joints.size(); ++guard) {
        const auto joint =
            std::ranges::find(project.joints, child, &compiler::ir::Joint::child_body);
        if (joint == project.joints.end())
            break;
        chain.push_back(&*joint);
        if (joint->parent_body == "WORLD")
            break;
        child = joint->parent_body;
    }
    std::ranges::reverse(chain);
    return chain;
}

} // namespace

auto joint_values_at(const compiler::ir::Scene& scene, double time_seconds) -> JointValues {
    JointValues values;
    for (const auto& track : scene.tracks) {
        if (track.target_kind == "JOINT")
            values[track.target] = sample_track(track, time_seconds);
    }
    return values;
}

auto transform_joint_point(const compiler::ir::Project& project, std::string_view body,
                           cad::Point3 point, const JointValues& values) -> cad::Point3 {
    std::vector<Operation> applied;
    for (const auto* joint : joint_chain(project, body)) {
        const auto pivot_source =
            std::ranges::find(project.points, joint->point, &compiler::ir::SpatialPoint::name);
        const auto axis_source =
            std::ranges::find(project.vectors, joint->axis, &compiler::ir::Direction::name);
        if (pivot_source == project.points.end() || axis_source == project.vectors.end())
            continue;
        cad::Point3 pivot{pivot_source->position_mm[0], pivot_source->position_mm[1],
                          pivot_source->position_mm[2]};
        cad::Vector3 axis{axis_source->unit[0], axis_source->unit[1], axis_source->unit[2]};
        for (const auto& operation : applied) {
            if (operation.kind == compiler::ir::JointKind::revolute) {
                rotate_axis(pivot, operation.pivot, operation.axis, operation.delta);
                cad::Point3 direction{axis.x, axis.y, axis.z};
                rotate_axis(direction, {}, operation.axis, operation.delta);
                axis = {direction.x, direction.y, direction.z};
            } else if (operation.kind == compiler::ir::JointKind::prismatic) {
                pivot.x += operation.axis.x * operation.delta;
                pivot.y += operation.axis.y * operation.delta;
                pivot.z += operation.axis.z * operation.delta;
            }
        }
        const auto requested = values.find(joint->name);
        const double target = requested == values.end() ? joint->value : requested->second;
        const double delta = target - joint->value;
        if (joint->kind == compiler::ir::JointKind::revolute) {
            rotate_axis(point, pivot, axis, delta);
        } else if (joint->kind == compiler::ir::JointKind::prismatic) {
            point.x += axis.x * delta;
            point.y += axis.y * delta;
            point.z += axis.z * delta;
        }
        applied.push_back({joint->kind, pivot, axis, delta});
    }
    return point;
}

auto sample_model(const compiler::ir::Project& project, const cad::Model& rest_model,
                  const compiler::ir::Scene& scene, double time_seconds) -> cad::Model {
    auto sampled = rest_model;
    const auto values = joint_values_at(scene, time_seconds);
    for (auto& part : sampled.parts) {
        for (auto& vertex : part.vertices)
            vertex = transform_joint_point(project, part.body, vertex, values);
    }
    return sampled;
}

} // namespace icad::scene
