#include "icad/compiler/resolver/resolver.hpp"

#include <iostream>

auto main() -> int {
    icad::compiler::ast::Program program;
    program.project_name = "duplicates";
    program.parameters.push_back({"span", {1, "m", {1, 16}}, {1, 1}});
    program.parameters.push_back({"span", {2, "m", {2, 16}}, {2, 1}});
    program.bodies.push_back({"deck", {}, {}, {}, {3, 1}});
    program.bodies.push_back({"deck", {}, {}, {}, {4, 1}});

    const auto result = icad::compiler::resolve(program);
    if (result.ok() || result.diagnostics.size() != 2) {
        std::cerr << "resolver did not reject both duplicate declarations\n";
        return 1;
    }
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code != "ICAD-R0001") {
            std::cerr << "resolver returned an unstable diagnostic code\n";
            return 1;
        }
    }
    return 0;
}
