#pragma once

#include "model.hpp"

#include "icad/compiler/ir/ir.hpp"

#include <string>
#include <vector>

namespace icad::cad {

struct BooleanResult {
    Part part;
    std::vector<std::string> repairs;
};

[[nodiscard]] auto apply_boolean(const Part& first, const Part& second,
                                 compiler::ir::FeatureOperation operation,
                                 std::string result_name) -> BooleanResult;

} // namespace icad::cad
