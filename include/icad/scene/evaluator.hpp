#pragma once

#include "icad/cad/model.hpp"
#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace icad::scene {

using JointValues = std::unordered_map<std::string, double>;

[[nodiscard]] auto joint_values_at(const compiler::ir::Scene& scene, double time_seconds)
    -> JointValues;
[[nodiscard]] auto transform_joint_point(const compiler::ir::Project& project,
                                         std::string_view body, cad::Point3 point,
                                         const JointValues& values) -> cad::Point3;
[[nodiscard]] auto sample_model(const compiler::ir::Project& project,
                                const cad::Model& rest_model,
                                const compiler::ir::Scene& scene,
                                double time_seconds) -> cad::Model;

} // namespace icad::scene
