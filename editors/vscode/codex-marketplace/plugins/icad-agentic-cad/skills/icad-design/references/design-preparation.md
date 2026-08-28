# Design preparation record

Complete this record after the single `icad.agent.conceptualize` call and before
writing geometry. Its purpose is to turn a short prompt into a traceable
engineering definition, not to add prose around an already chosen shape.

## Evidence classes

Label every design decision as one of:

- **given**: explicitly supplied by the user, drawing, datasheet, or existing model;
- **derived**: calculated from given values, with the relationship recorded;
- **assumed**: a reversible non-critical default chosen to keep work moving;
- **open**: a missing decision that changes fit, safety, architecture, or manufacturing.

Do not convert an open decision into an invented fact. Ask one concise question
when it controls safety, fit, external compatibility, or the body graph. For
ordinary missing detail, choose an editable named parameter, record the
assumption, and continue.

## Required record

1. **Purpose and operating conditions** — function, user interaction,
   environment, expected loads, duty cycle, service life, and failure modes.
2. **Coordinate system and envelope** — units, origin, axes, maximum XYZ bounds,
   keep-out zones, reference planes, and required clearances.
3. **Component architecture** — BOM, quantity, role, ownership, grounded body,
   replaceable parts, symmetry, and parent-child graph.
4. **Per-part construction** — material, manufacturing process, stock form,
   primary datum, minimum wall or section, ordered sketch/pad/pocket history,
   finish, and critical features.
5. **Interfaces and hardware** — both mating sides, location, axis, connection
   method, real standard, fastener or fit, engagement length, tool access,
   assembly direction, and service clearance.
6. **Kinematics** — DOF, joint type, anchor, axis, limits, home state, driven
   state, collision envelope, and what remains fixed.
7. **Tolerance strategy** — general tolerance plus critical fits, hole sizes,
   edge distances, alignment, process allowance, and inspection dimensions.
8. **Visual and ergonomic intent** — identifying silhouette, orientation,
   required views, exposed controls/connectors, cable paths, and appearance.
9. **Deliverables and acceptance** — required source files and exports plus
   measurable compile, geometry, interface, interference, motion, and read-back
   checks.

Quantify dimensions. Words such as *small*, *strong*, *professional*, *low
profile*, or *tight fit* are not specifications until converted into a named
parameter or acceptance limit.

## Traceability gate

Before source generation, map each requirement to at least one planned ICAD
entity and one verification method:

```text
requirement -> PARAMETER/BODY/FEATURE/INTERFACE/JOINT -> visual or numeric check
```

Reject the preparation record when a component lacks a functional role, an
interface has only one mating side, a movable body lacks a joint contract, or a
critical dimension has no source or assumption label.

## Professional geometry rules

- Model functional details that affect fit, manufacturing, assembly, or the
  recognizable silhouette. Decorative detail is secondary.
- Never shrink, flatten, hide, or replace real hardware merely to make an
  interference test pass. Fastener diameter, head, shaft, engagement, and
  clearance must agree with the recorded standard and dimensional stack.
- Do not claim threads, bearings, fits, fillets, fasteners, or materials that
  are absent from source or unsupported by the compiler.
- For multi-file projects, keep one authoritative parameter set. If the current
  language requires assembly geometry to be repeated in `main.icad`, compare
  it with the part file and reject dimensional drift.
- A clean compile is not visual acceptance. The model must render, each body
  must have nonzero useful geometry, four-view silhouettes must be coherent,
  and interfaces must be seated without unintended penetration.

Keep the record concise enough to inspect, but complete enough that another
agent can reproduce the intended model without guessing the architecture.
