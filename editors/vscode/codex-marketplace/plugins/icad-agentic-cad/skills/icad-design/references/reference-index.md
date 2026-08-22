# ICAD design reference index

Use these references together, with a strict authority order:

1. The running compiler's `icad.language` response defines accepted syntax.
2. `icad-grammar.ebnf` is the packaged, offline grammar snapshot.
3. `modeling-contract.md` defines the sketch-to-assembly modeling discipline.
4. `blueprint-concept-pass.md` is the token-efficient operational extraction.
5. `blueprint-reading-complete.pdf` is the page-preserving source reference.

For ordinary ICAD generation, read the Markdown concept pass and modeling
contract, then consult the EBNF for exact productions. Do not inject the full
PDF into every prompt. Consult the PDF only for drawing conventions, view
selection, title blocks, dimensions, tolerances, sections, auxiliary views, or
other page-level evidence not represented in the compressed guide.

For generated source, begin with `REQUIRES ICAD 1.0` and add only capability
names returned by the running compiler. Requirement headers are a compatibility
contract, not permission to emit proposal-only syntax.

Blueprint knowledge controls how design intent is interpreted. ICAD grammar
controls how that intent is expressed. If the packaged EBNF and the running
compiler disagree, use `icad.language`, report the mismatch, and never invent
syntax.

Packaged source metadata:

- File: `blueprint-reading-complete.pdf`
- Pages: 72
- SHA-256: `049fd7bf224fe7de9278196f416d0b14e2042dab08e14d25b1f13ebfd2b878df`
- Grammar source: repository `grammar/icad.ebnf`
