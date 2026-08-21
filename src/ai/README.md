# AI integration

This module exposes deterministic `icad.inspect.v1` and
`icad.diagnostics.v1` JSON. `inspect-json` combines canonical IR counts,
revision fingerprint, geometry metrics, per-body bounds/centers/sizes, named
body surface/volume contacts, resolved spatial expressions with provenance,
poses, mechanism joints and degrees of freedom, mate/constraint results, scene
track ranges, and manufacturing status.
`diagnostics-json` reports stable codes and source
locations even when compilation fails. Deterministic prompt intent routing and
maintained design templates live in `src/agent`; open-ended design reasoning
stays in the agent/plugin layer.

`icad.agent.review` selects the spatially useful subset of inspection as
`icad.agent.design-map.v1`: total and per-body bounds, named points/vectors,
joint connectivity and limits, constraints/mates, contacts, and animation. This
is intentionally semantic rather than an unstructured dump of mesh vertices.
