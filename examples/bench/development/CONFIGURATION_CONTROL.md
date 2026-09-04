# Configuration and evidence control

- `../turbojet_engine.icad` remains the design authority.
- `../baseline/R3_BASELINE.json` identifies the immutable packaging baseline.
- `../turbojet_engine.evidence.json` is the machine-readable evidence index.
- Every partner artifact is copied beneath this directory, hashed with SHA-256,
  assigned a unique ID, and linked to model revision, model digest, requirement,
  author, reviewer, disposition, units, assumptions, margins, and limitations.
- Accepted evidence requires an independent reviewer distinct from the author.
- Critical parts require serialized identity, material certificate, process route,
  first-article inspection, NDT disposition, balance record, and life status.
- Changes use a signed change request containing affected requirements, analyses,
  drawings, tooling, tests, hazards, and evidence invalidation.
- Non-conformances remain open until use-as-is, repair, rework, or scrap disposition
  is signed by the responsible engineering and quality authorities.
