# Project artifact builder

This module validates a canonical project and stages the complete STEP,
assembly STEP, STL, OBJ, web viewer, scene, BOM, manufacturing, and drawing
package before committing named artifacts to the destination. CLI and MCP use
this same API so their output behavior cannot drift.
