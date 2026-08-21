#include "icad/agent/workflow.hpp"
#include "icad/compiler/compiler.hpp"

#include <iostream>
#include <string_view>

namespace {

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

auto main() -> int {
    const auto robotic = icad::agent::bootstrap(
        "Create an articulated industrial robotic arm with a gripper and animated joints");
    if (robotic.intent != icad::agent::DesignIntent::robotic_arm ||
        !robotic.json.contains("\"expectedModelIterations\":1") ||
        !robotic.json.contains("\"preferredTool\":\"icad.agent.create\"") ||
        !robotic.json.contains("\"selectedTemplate\":\"robotic_arm\"") ||
        !robotic.json.contains("\"sourceReference\":\"examples/Robotic_Arm_3D_Model\"") ||
        !robotic.json.contains("\"name\":\"joint_radius\"") ||
        !robotic.json.contains("\"name\":\"elbow_angle\"") ||
        !robotic.source.contains("# Agent prompt: Create an articulated industrial robotic arm") ||
        !robotic.source.contains("# Embedded template: ROBOTIC_ARM") ||
        !robotic.json.contains("\"workflow\":[\"agent.create\"]")) {
        return fail("robotic-arm prompt did not select the low-iteration workflow");
    }
    const auto compilation = icad::compiler::compile(robotic.source);
    if (!compilation.ok() || compilation.ir_project->bodies.size() != 10 ||
        compilation.ir_project->joints.size() != 10) {
        return fail("embedded robotic-arm scaffold is not the maintained detailed model");
    }
    const auto review = icad::agent::review_json(robotic.source);
    if (!review.contains("\"ready\":true") || !review.contains("\"bodies\":10") ||
        !review.contains("\"joints\":10") ||
        !review.contains("\"degreesOfFreedom\":7") ||
        !review.contains("\"topologyValid\":true") ||
        !review.contains("\"schema\":\"icad.agent.design-map.v1\"") ||
        !review.contains("\"centerMm\":[0,0,28]") ||
        !review.contains("\"name\":\"elbow_hinge\"") ||
        !review.contains("\"child\":\"arm_02\"") ||
        !review.contains("\"name\":\"preview_forearm_axis\"") ||
        !review.contains("\"name\":\"tool_on_target\"") ||
        !review.contains("\"name\":\"articulation\"")) {
        return fail("composite robotic-arm readiness review is incomplete");
    }

    const auto generic = icad::agent::bootstrap("Make a small mounting block");
    if (generic.intent != icad::agent::DesignIntent::generic_part ||
        !icad::compiler::compile(generic.source).ok()) {
        return fail("generic prompt did not produce a valid parametric starter");
    }
    const auto multiline = icad::agent::bootstrap("robot arm\nPROJECT injected");
    if (!icad::compiler::compile(multiline.source).ok() ||
        !multiline.source.contains("# Agent prompt: robot arm PROJECT injected") ||
        multiline.source.contains("\nPROJECT injected\n")) {
        return fail("prompt provenance allowed source-comment injection");
    }
    if (!icad::agent::review_json("PROJECT broken\n$").contains("\"stage\":\"compile\"")) {
        return fail("agent review did not focus invalid source on compiler repair");
    }
    return 0;
}
