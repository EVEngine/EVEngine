# Canonical material v5: motion highlight

Historical format contract: the current format is [material v6](canonical-material-v6.md).
V5 remains the N-1 read/migration format. Material/4 is outside the current
window. The historical 4-to-5 migration preserved existing fields and identity,
including translucency, and rejects unversioned `motionHighlightColor` in v4.

V5 adds optional `motionHighlightColor`, exactly three finite nonnegative linear
RGB numbers. HDR values above one are accepted; omission means zero. The value
belongs to the material snapshot. The per-vertex evaluated scalar belongs to
the mesh, and globals/time remain owned by the vegetation evaluator. No new
clock, persistent mesh channel, or cross-domain owner is introduced.

The runtime reader accepts v5 and v4; v4 cannot carry the new field. Existing v4
translucency remains supported. The Vulkan shader multiplies final albedo after
capturing the earlier translucency albedo. A missing runtime highlight stream
contributes zero.

The Plant Standard and Plant Subsurface import paths now emit v5 and map the
source `[HDR] _MotionHighlightColor` without gamma conversion. Prop
families do not acquire Plant highlight semantics. glTF and Unity Standard
producers emit v4 for migration to v5. An isolated Unity 6000.0.79f1 GPU oracle
saved/reloaded materials with this HDR property and read RGBAFloat shader output
in both Linear and Gamma modes. Stored and GPU values match for subunit and
HDR inputs, including the original Grass_Meadow RGB. This corrected the initial
gamma-conversion implementation. Results:
build/tve-unity-normal-oracle/HighlightColorExports/results.json.

Reader tests cover HDR ownership, v4 rejection, negative values and vector shape.
Build and focused reader execution are pending. Migration tests, other producer
version assertions, full related regression tests, original source import/cook,
backend parity and fresh architecture gates must be updated and run before this
format transition is considered complete. Earlier v4 test/cook evidence remains
historical and does not prove the new v5 transition.

The v5 build and all five focused material-reader tests now pass. Evidence:
build/vegetation-material5-reader-tests.log. The broader transition obligations
above remain pending.

The related regression run completed with 181/182 passing. The sole failure was
the existing terrain test's temporary-document open in the sandbox; rerunning
that exact test with normal host permissions passed. Version expectations now
match v5 cooking and v4 generic producers. Additional migration/source-import
assertions were then rebuilt and all three focused cases passed: v4 translucency
is retained by migration, unversioned highlight is rejected, and Plant highlight
RGB survives import/cook/runtime reading. Evidence:
build/vegetation-material5-regression-tests.log,
build/vegetation-material5-terrain-tests.log and
build/vegetation-material5-final-tests.log. This is not yet original HDR-color
oracle evidence or an original-source v5 package cook.

The subsequent HDR oracle above supplies the missing color-space evidence.
The importer was corrected to preserve these HDR values directly, and its
import/cook/runtime test now checks 2.1185474 and 0.5 unchanged. Rebuild and this
focused test pass: build/vegetation-highlight-hdr-tests.log. The original-source
v5 package cook remains outstanding.

The CLI has now been rebuilt with the HDR correction and the original 32-material
source directory is being reimported to build/tve-materials-highlight.eva using
the same package identity as v4. The prepared independent validator checks every
Plant RGB directly against the source, absence on Prop shaders, equality of all
previous material fields, and exact bytes for all 172 images. Its syntax check
passes; execution awaits the import. A fresh v5 architecture gate and fixtures
have started. Module layering passes with no back-edges:
build/vegetation-material5-layering.log.

## HDR correction supersedes earlier color evidence

An ordinary-Color control shader confirmed that Unity's Linear path converts
ordinary properties, while the HDR property preserves stored values. Source
inspection then confirmed `_MainColor`, `_MainColorTwo` and `_SubsurfaceColor`
are also `[HDR]` in the supported TVE Plant/Prop shader families. Their previous
gamma conversion was therefore incorrect. The importer now preserves these
values directly, and the synthetic GPU fixture uses the actual stored 0.5 tint
rather than encoding it to gamma first. This correction is rebuilding/testing.

Earlier v4 exact archive/cook comparisons and fern pixel results remain evidence
of internal consistency, not Unity color fidelity. The currently running v5
import was launched before this correction and must not be claimed as corrected
source-color evidence. It will be allowed to finish; a corrected import requires
rebuilding the CLI afterward. The original-source validator must also be updated
to independently check corrected HDR colors rather than requiring equality with
the erroneous v4 values. Oracle control results:
build/tve-unity-normal-oracle/StandardColorExports/results.json.

The all-HDR correction now builds and all nine focused import/GPU cases pass
under Vulkan validation: build/vegetation-all-hdr-tests.log. Corrected original
resource reimport remains pending the older CLI import's completion.

The original validator now targets build/tve-materials-hdr.eva and independently
reads all four HDR color families from Unity material text. It substitutes only
those corrected colors into the historical comparison, preserving exact checks
for every other preceding field and all image payloads. Its syntax check passes;
the corrected archive does not exist yet. The v5 architecture main check passed;
its fixture process remains active. The preceding CLI import is still live and
will not be restarted or overwritten based on elapsed time.

The earlier `tve-materials-highlight.eva` import has since completed and is
retained only as pre-HDR-correction evidence. The corrected CLI was rebuilt and
is now importing the source to `tve-materials-hdr.eva`; that process remains
live. The imported-material GPU composition also now combines an imported v5
highlight color with a mesh highlight stream. It matches the independently
configured highlighted reference under Vulkan validation:
build/vegetation-import-highlight-gpu-tests.log.

After changed-line formatting, the full related selection reached 181/182
passes. Its sole failure is the same existing terrain temporary-document open;
the exact test passes with normal host permissions. Evidence:
build/vegetation-v5-formatted-tests.log and
build/vegetation-material5-terrain-tests.log. Test discovery passes for all 617
sources. This evidence does not replace the pending corrected original import.

The corrected original import has now completed. Independent source validation
passes for all 32 materials: 21 Plant materials carry the versioned highlight
field, three have nonzero highlight RGB, all four HDR color families match Unity
source values directly, and all 172 image definitions/payloads remain unchanged.
Results: build/tve-reference/original-highlight-validation.json. The full v5
runtime pack cook has started; cooked-definition and original GPU validation are
pending.
