#pragma once

#include "icad/compiler/ir/ir.hpp"

namespace icad::constraints {

// Solves a dimensional sketch in place and records convergence, rank, and DOF.
// Fixed points are never modified. The implementation is deterministic and
// dependency-free so every compiler consumer receives identical coordinates.
auto solve_sketch(compiler::ir::Sketch& sketch) -> void;

} // namespace icad::constraints
