#include "map/spawn/editor/PreparedSpawnExport.h"
#include "filesystem/AtomicTextWriteLease.h"
#include "map/spawn/editor/SpawnForm.h"
namespace eve::spawn_editor {
PreparedSpawnExport::PreparedSpawnExport(std::string json, eve::Revision revision)
    : json_(std::move(json)), revision_(revision) {}
Result<PreparedSpawnExport> PreparedSpawnExport::prepare(std::shared_ptr<SpawnForm> form) {
    using R = Result<PreparedSpawnExport>;
    if (!form || form->history().pending)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Export requires a form with no unresolved transaction"));
    auto snapshot = form->captureSource();
    if (!snapshot) return R::failure(snapshot.status());
    auto json = snapshot.value().source.toJson();
    if (!json) return R::failure(json.status());
    return R::success(PreparedSpawnExport(std::move(json).takeValue(), snapshot.value().revision));
}
const std::string& PreparedSpawnExport::json() const noexcept { return json_; }
eve::Revision      PreparedSpawnExport::sourceRevision() const noexcept { return revision_; }
Result<void>       PreparedSpawnExport::write(const filesystem::AtomicTextWriteLease& filesystem,
                                              std::string_view                        relativePath) const {
    return filesystem.write(relativePath, json_);
}
}  // namespace eve::spawn_editor
