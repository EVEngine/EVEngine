#pragma once

#include "asset/import/AssetImporter.h"

namespace eve::asset_import {

/** @brief Owning LDraw source set. Names are relative, case-insensitive LDraw paths.
 * Supply the document and referenced parts (including LDConfig.ldr for named colours).
 * Sources are borrowed only during preparation; no implicit filesystem/network lookup occurs.
 */
struct LDrawImportRequest {
    ImportPackageIdentity                            package;
    std::string                                      sourceName;
    std::map<std::string, std::vector<std::uint8_t>> files;
    AssetImportLimits                                limits;
    std::uint32_t                                    maximumDepth         = 64;
    std::uint64_t                                    maximumExpandedLines = 2'000'000;
};

/** @brief Owning OpenSCAD source and its compiler-produced binary STL.
 * STL is geometry only: colour and animation are not inferred from the mesh bytes.
 * Optional named part STLs (`partStls`) and an `eve.scad-rig/1` document (`rigBytes`) retain
 * rotatable joint metadata for mechanical assemblies. OpenSCAD millimetres/Z-up become metres/Y-up.
 * The root source (and rig text when provided) are preserved.
 */
struct ScadImportRequest {
    ImportPackageIdentity                            package;
    std::string                                      sourceName;
    std::vector<std::uint8_t>                        sourceBytes;
    std::vector<std::uint8_t>                        binaryStl;
    std::map<std::string, std::vector<std::uint8_t>> partStls;
    std::vector<std::uint8_t>                        rigBytes;
    AssetImportLimits                                limits;
};

/** @brief Prepare LDR/DAT/MPD triangles, referenced parts and colour-separated static prefab.
 * @param request Borrowed source set and budgets, valid only for this synchronous preparation.
 * @return Owning candidate or checked parse/reference/budget/unsupported diagnostics; no partial publication.
 * @thread Worker-safe and reentrant; synchronous CPU work, no IO, callbacks or retained pointers.
 * @cost More expensive than loading cooked meshes; expands references and copies triangles once per import.
 * @details BFC winding and INVERTNEXT are applied. Lines/conditional lines are reported but not rendered.
 * Unknown geometry-affecting extensions fail explicitly; this is not a LEGO connectivity/physics importer.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<PreparedAssetImport> prepareLDrawImport(const LDrawImportRequest& request);

/** @brief Validate compiler-produced binary STL part(s) and prepare a canonical static prefab.
 * @param request Borrowed root source, STL bytes (single `binaryStl` or named `partStls`), optional ScadRig
 *        JSON and budgets; no input reference is retained.
 * @return Owning candidate or checked format/range/budget/rig diagnostic, without publishing state.
 * @thread Worker-safe and reentrant; no IO, callbacks or retained pointers.
 * @cost Copies and encodes every triangle once per import; amortize by cooking and reusing the package.
 * @details When `partStls` is empty the legacy single mesh `"default"` path is used. A present ScadRig is
 * validated and published as entrypoint `rig` (`eve.scad-rig/1`); joint angles are not baked into the
 * static prefab — runtime posing remains a Scene/`Renderable3D` composition concern.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<PreparedAssetImport> prepareScadImport(const ScadImportRequest& request);

/** @brief Desktop tool request for compiling a SCAD file with an installed OpenSCAD executable.
 * The compiler may resolve include/use/import files as OpenSCAD normally does. This is a local authoring
 * operation, not a sandbox. The generated package retains the root source, not its dependency closure.
 * When `<stem>.rig.json` sits beside the `.scad` input, the importer loads that `eve.scad-rig/1`
 * sidecar, compiles each listed part with `-D part="<name>"`, and attaches the rig — no extra CLI flags.
 */
struct ScadFileImportRequest {
    ImportPackageIdentity package;
    std::string           sourcePath;
    std::string           executable     = "openscad";
    std::uint32_t         timeoutSeconds = 120;
    AssetImportLimits     limits;
};

/** @brief Compile via argument-vector process launch, then prepare without publishing.
 * @param request Borrowed desktop compiler settings and source path, valid until this blocking call returns.
 * @return Owning candidate; absent compiler, timeout, nonzero exit and bad geometry are checked failures.
 * @thread Desktop authoring worker; reentrant, uses a private temporary directory, invokes no callbacks.
 * @cost External CSG compilation is substantially more expensive than runtime loading; cook once per edit.
 * @details Mobile/browser providers return Unsupported. Runtime consumers never require OpenSCAD.
 * A present `<stem>.rig.json` sidecar selects the multi-part mechanical path automatically.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<PreparedAssetImport> prepareScadFileImport(
    const ScadFileImportRequest& request);

}  // namespace eve::asset_import
