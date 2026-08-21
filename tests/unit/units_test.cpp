#include "icad/compiler/units/units.hpp"

#include <cmath>
#include <iostream>

auto main() -> int {
    using namespace icad::compiler::units;
    const auto metres = find("m");
    const auto millimetres = find("mm");
    const auto degrees = find("deg");
    if (!metres || !millimetres || !degrees) {
        std::cerr << "expected unit definition is missing\n";
        return 1;
    }
    const auto converted = convert(2.5, *metres, *millimetres);
    if (!converted || std::abs(*converted - 2500.0) > 0.000001) {
        std::cerr << "length conversion is incorrect\n";
        return 1;
    }
    if (convert(1.0, *metres, *degrees).has_value()) {
        std::cerr << "conversion crossed physical dimensions\n";
        return 1;
    }
    if (find("parsec").has_value()) {
        std::cerr << "unknown unit was accepted\n";
        return 1;
    }
    return 0;
}

