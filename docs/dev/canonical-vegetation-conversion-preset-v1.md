# Canonical vegetation conversion preset v1

`eve.vegetation-conversion-preset/1` is the lossless executable syntax boundary
for The Vegetation Engine 12.6 `.tvepreset` files. It keeps conversion policy out
of material and mesh definitions while allowing a later batch conversion stage
to resolve includes and evaluate source-material conditions deterministically.

The JSON definition contains `schema`, `schemaVersion`, `sourcePath` and an
ordered `statements` array. A command statement has `kind: command`, `domain`,
`operation` and an ordered string `arguments` array. A condition statement has
`kind: condition`, `predicate`, `negated`, `arguments` and nested `statements`.
Whitespace and separator banners are not semantic. Line comments are discarded.
Command and argument spelling remains source-authored.

Parsing is synchronous, reentrant and worker-safe. Input is borrowed for the
call; the candidate owns its JSON bytes. UTF-8, source size, line length,
statement count and nesting depth are bounded before publication. Unexpected or
unterminated braces fail the whole preset without a partial result.

TVE 12.6 contains one malformed `f SHADER_NAME_CONTAINS` line. The importer
interprets this specific leading token as `if`, retains the predicate and body,
and records `TVE.preset.conditionTypo` in the import report. Other malformed
block syntax is rejected.

Six source Include statements refer to singular `Use Default Flower Masks` or
`Use Default Flower Settings`, while the supplied include filenames use
`Flowers`. Evaluation contains only these two explicit 12.6 compatibility
aliases. Every other unresolved include remains a checked failure; there is no
case folding or fuzzy name lookup.

The supplied package contains 121 presets. All 121 imported into a valid
241,426-byte EVA and cooked into a valid 121-chunk Vulkan EVPACK. The EVA SHA-256
is `A47FC1125159DCE0716AA0D27BE68370FBB55920A0479F5CC4CFDAD0BED5F6FB`; the
EVPACK SHA-256 is `37737AFE0EABBAAA5179D1A6D06C5E6FB5FABF9CE6156DE82BE4FA32B7CAFF10`.
Focused tests cover nested conditions, Include retention, the known typo and
transactional rejection of an unterminated block.

`evaluateVegetationPreset` validates version-one definitions, expands Include
statements depth-first in source order and evaluates output-option, shader-name,
material-name, float, property, texture, keyword and render-pipeline predicates.
It returns one detached command sequence. Missing includes, include cycles,
unknown predicates and malformed predicate values fail without exposing a
partial sequence. Command type-checking and atomic application to conversion
candidates remain required before preset-driven batch conversion is complete.
