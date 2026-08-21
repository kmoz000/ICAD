#include "icad/cad/analysis.hpp"
#include "icad/compiler/compiler.hpp"
#include "icad/constraints/validator.hpp"

#include <cmath>
#include <iostream>

auto main() -> int {
    auto compiled = icad::compiler::compile(
        "PROJECT clearances\nUNITS mm\n"
        "BODY first\nFEATURE a\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\nEND\nEND\n"
        "BODY second\nFEATURE b\nTYPE BOX\nWIDTH 10 mm\nDEPTH 10 mm\nHEIGHT 10 mm\n"
        "ORIGIN_X 20 mm\nEND\nEND\n"
        "CONSTRAINT gap MIN_DISTANCE first second 5 mm\n");
    if (!compiled.ok()) {
        std::cerr << "constraint fixture did not compile\n";
        return 1;
    }
    const auto analysis = icad::cad::analyze(*compiled.ir_project);
    if (analysis.parts.size() != 2 || std::abs(analysis.volume_mm3 - 2000.0) > 1e-6 ||
        analysis.bounds.maximum[0] != 30.0) {
        std::cerr << "geometry analysis returned incorrect metrics\n";
        return 1;
    }
    const auto passing = icad::constraints::validate(*compiled.ir_project);
    if (passing.size() != 1 || !passing.front().passed || passing.front().actual_mm != 10.0) {
        std::cerr << "valid clearance constraint did not pass\n";
        return 1;
    }
    compiled.ir_project->constraints.front().minimum_mm = 15.0;
    const auto failing = icad::constraints::validate(*compiled.ir_project);
    if (failing.front().passed) {
        std::cerr << "invalid clearance constraint did not fail\n";
        return 1;
    }
    return 0;
}
