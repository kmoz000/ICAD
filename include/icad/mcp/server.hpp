#pragma once

#include <filesystem>
#include <iosfwd>

namespace icad::mcp {

auto run(std::istream& input, std::ostream& output,
         const std::filesystem::path& workspace) -> int;

} // namespace icad::mcp
