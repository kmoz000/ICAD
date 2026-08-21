# Manufacturing semantics

This module will own manufacturing intent and validation. CAM remains a
separate backend and must consume stable ICAD IR rather than compiler internals.

The implemented first stage checks nonzero volume, configurable minimum and
maximum XYZ extents, and body material assignment. `icad build` writes the
result as `.manufacturing.json`. Wall thickness, tooling access, tolerances,
and process-specific rules remain future native work.
