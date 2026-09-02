#include <utility>
#include "sceneloader/SceneLoader.h"
#include "sceneloader_editing/SceneImportTarget.h"
namespace eve::sceneloader_editing {
namespace {
template <class T>
EditorResult<T> fail(EditorStatus s, const char* r, std::string m) {
    return eve::editing::failed<T>(s, RuleId(r), std::move(m));
}
}  // namespace
EditorResult<SceneImportPreflight> SceneImportPreflightRuntime::inspect(
    const SceneImportTarget& target, sceneloader::SceneLoader* loader) const {
    if (!loader)
        return fail<SceneImportPreflight>(EditorStatus::Rejected, "editor.scene-import.loader",
                                          "SceneLoader is required");
    auto diagnostics = target.validate();
    for (const auto& d : diagnostics)
        if (d.severity() == DiagnosticSeverity::Error)
            return EditorResult<SceneImportPreflight>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    const auto&              v = target.value();
    sceneloader::LoadOptions o;
    o.triangulate              = v.triangulate;
    o.generateNormalsIfMissing = v.generateNormals;
    o.joinIdenticalVertices    = v.joinVertices;
    o.flipUVs                  = v.flipUvs;
    o.improveCacheLocality     = v.improveCache;
    o.sharedMeshes             = v.sharedMeshes;
    o.mipmaps                  = v.mipmaps;
    o.importLights             = v.importLights;
    o.importCameras            = v.importCameras;
    o.importAnimations         = v.importAnimations;
    auto inspected             = loader->inspect(v.sourceAsset, o);
    if (!inspected.hasValue()) {
        const auto* primary = inspected.status().primaryDiagnostic();
        return fail<SceneImportPreflight>(EditorStatus::Failed, "editor.scene-import.decode",
                                          primary ? primary->message() : "Scene import inspection failed");
    }
    const auto&          s = inspected.value();
    SceneImportPreflight out;
    out.sourceRevision = target.revision();
    out.nodes          = s.nodeCount;
    out.meshNodes      = s.meshNodeCount;
    out.added          = s.diff.added;
    out.removed        = s.diff.removed;
    out.modified       = s.diff.modified;
    out.moved          = s.diff.moved;
    out.warnings       = s.warnings;
    out.sockets        = s.sockets;
    out.collisions     = s.collisions;
    for (const auto& w : s.warnings)
        diagnostics.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::Failed,
            RuleId("editor.scene-import.warning"), DiagnosticSeverity::Warning, w));
    return eve::editing::applied<SceneImportPreflight>(std::move(out), std::move(diagnostics));
}
}  // namespace eve::sceneloader_editing
