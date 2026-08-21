#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>

namespace {

[[nodiscard]] auto has_code(const icad::compiler::CompileResult& result,
                            std::string_view code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

auto main() -> int {
    const auto valid = icad::compiler::compile(
        "PROJECT semantic_test\nUNITS cm\n"
        "BODY bridge\nFEATURE deck\nTYPE BOX\n"
        "WIDTH 2 m\nDEPTH 50 cm\nHEIGHT 4 mm\nORIGIN_X 1 m\n"
        "END\nEND\n");
    if (!valid.ok()) {
        std::cerr << "semantic pipeline rejected valid mixed units\n";
        return 1;
    }
    const auto& properties = valid.ir_project->bodies.front().features.front().properties;
    if (properties.size() != 4 || std::abs(properties.front().value.value - 2000.0) > 0.000001 ||
        properties.front().value.unit != "mm") {
        std::cerr << "semantic lowering did not canonicalize length units\n";
        return 1;
    }

    const auto missing = icad::compiler::compile(
        "PROJECT missing\nUNITS mm\nBODY b\nFEATURE f\nTYPE BOX\n"
        "WIDTH 1 mm\nDEPTH 1 mm\nEND\nEND\n");
    if (missing.ok() || !has_code(missing, "ICAD-S0010")) {
        std::cerr << "missing required geometry property was accepted\n";
        return 1;
    }

    const auto wrong_dimension = icad::compiler::compile(
        "PROJECT dimensions\nUNITS mm\nBODY b\nFEATURE f\nTYPE BOX\n"
        "WIDTH 1 deg\nDEPTH 1 mm\nHEIGHT 1 mm\nEND\nEND\n");
    if (wrong_dimension.ok() || !has_code(wrong_dimension, "ICAD-S0002")) {
        std::cerr << "physical dimension mismatch was accepted\n";
        return 1;
    }

    const auto parameter_reference = icad::compiler::compile(
        "PROJECT references\nUNITS mm\nPARAMETER width 2 m\nBODY b\nFEATURE f\nTYPE BOX\n"
        "WIDTH width\nDEPTH 10 mm\nHEIGHT 10 mm\nEND\nEND\n");
    if (!parameter_reference.ok() ||
        std::abs(parameter_reference.ir_project->bodies.front().features.front()
                     .properties.front().value.value -
                 2000.0) > 0.000001) {
        std::cerr << "parameter-backed feature property was not resolved\n";
        return 1;
    }
    return 0;
}
