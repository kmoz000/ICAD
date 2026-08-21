# Document model

This module owns deterministic project fingerprints, exact hexadecimal source
revisions, compiler-validated atomic source commits, cross-process optimistic
concurrency, immutable source history, restore, parameter transactions,
in-memory IR undo/redo, structural count diffs, and JSON BOM export. Feature
dependency recomputation remains future work. It consumes canonical IR without
exposing mesh indices to agents.
