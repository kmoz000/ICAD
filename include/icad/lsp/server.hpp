#pragma once

#include <iosfwd>

namespace icad::lsp {

auto run(std::istream& input, std::ostream& output) -> int;

} // namespace icad::lsp
