#pragma once

#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <vector>

namespace icad::constraints {

struct Result {
    std::string name;
    bool passed{false};
    double required_mm{};
    double actual_mm{};
    std::string unit{"mm"};
    std::string message;
};

[[nodiscard]] auto validate(const compiler::ir::Project& project) -> std::vector<Result>;
[[nodiscard]] auto all_passed(const std::vector<Result>& results) -> bool;

} // namespace icad::constraints
