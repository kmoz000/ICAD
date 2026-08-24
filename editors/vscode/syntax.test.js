const assert = require("assert");
const fs = require("fs");
const path = require("path");

const root = __dirname;
const syntax = JSON.parse(fs.readFileSync(path.join(root, "syntaxes/icad.tmLanguage.json"), "utf8"));
const theme = JSON.parse(fs.readFileSync(path.join(root, "themes/icad-industrial-dark-color-theme.json"), "utf8"));
const manifest = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));
const syntaxText = JSON.stringify(syntax);
const themeText = JSON.stringify(theme);

for (const scope of [
  "variable.other.parameter.definition.icad",
  "entity.name.point.icad",
  "entity.name.line.icad",
  "entity.name.curve.icad",
  "support.type.surface.operation.icad",
  "entity.name.object.icad",
  "entity.name.event.icad",
  "support.type.constraint.icad"
]) {
  assert(syntaxText.includes(scope), `syntax grammar omits ${scope}`);
  assert(themeText.includes(scope), `ICAD theme omits ${scope}`);
}
assert(syntaxText.includes("TANGENT"), "syntax grammar omits TANGENT");
for (const token of ["SELECTION", "EDGESET", "EDGES", "WHERE", "LOOP", "CIRCULAR", "CONCAVE", "CONVEX", "ADJACENT_TO"]) {
  assert(syntaxText.includes(token), `syntax grammar omits ${token}`);
}
assert.strictEqual(manifest.contributes.themes[0].label, "ICAD Industrial Dark");
assert(manifest.files.includes("themes/**"), "VSIX package omits themes");
