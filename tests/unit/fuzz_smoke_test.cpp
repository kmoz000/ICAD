#include "icad/compiler/compiler.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view seed_source =
    "PROJECT FuzzSeed\nUNITS mm\nBODY part\nFEATURE cube\nTYPE BOX\nWIDTH 10 mm\n"
    "DEPTH 20 mm\nHEIGHT 30 mm\nEND\nEND\n";

auto random(std::uint64_t& state) -> std::uint64_t {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

} // namespace

auto main() -> int {
    std::uint64_t state = 0x1CADEFACE1234567ULL;
    std::size_t accepted{};
    std::size_t rejected{};
    for (std::size_t iteration = 0; iteration < 1024; ++iteration) {
        std::string source{seed_source};
        const auto mutations = 1 + random(state) % 12;
        for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
            const auto action = random(state) % 3;
            const auto position = static_cast<std::size_t>(random(state) % (source.size() + 1));
            if (action == 0 && !source.empty()) {
                source.erase(std::min(position, source.size() - 1), 1);
            } else if (action == 1) {
                source.insert(position, 1, static_cast<char>(32 + random(state) % 95));
            } else if (!source.empty()) {
                source[std::min(position, source.size() - 1)] =
                    static_cast<char>(9 + random(state) % 118);
            }
        }
        if (icad::compiler::compile(source).ok())
            ++accepted;
        else
            ++rejected;
    }
    if (accepted == 0 || rejected == 0) {
        std::cerr << "deterministic compiler fuzz smoke did not exercise both outcomes\n";
        return 1;
    }
    return 0;
}
