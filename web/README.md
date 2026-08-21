# Web viewer

`icad-viewer.js` is ICAD's plain JavaScript Canvas library. Scene export embeds
its source in the compiler and writes a reusable local copy plus a minimal HTML
page and compiled data script. It has no desktop, npm, CDN, or framework
dependency.

Viewer implementation belongs in this directory. Compiler-side scene and
bundle generation belongs in `src/scene/`.
