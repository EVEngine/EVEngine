#pragma once
/** @file SpawnExportPanel.h
 * @brief spawn_editor UI face: explicit runtime JSON snapshot preparation and atomic export controls.
 */
#include <memory>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::ui {
class Component;
}
namespace eve::filesystem {
class AtomicTextWriteLease;
}
namespace eve::spawn_editor {
class SpawnForm;
/** @brief Create mountable relative-path, prepare and write controls for a real configuration form.
 * @return Owning component or missing form/filesystem error; performs no I/O or source mutation at creation.
 * @ownership Shares the actual form and copies a provider-specific weak atomic writer lease. Callback state outlives
 * widget trees; component destruction closes actions. Provider expiry rejects writes; no fake shared ownership.
 * @thread UI/authoring and filesystem owner thread; no locks, callbacks reject reentry and stale controls.
 * @remarks Changing path discards the prepared export. Source edits retain frozen bytes and visibly label the older
 * revision; writing that revision remains explicit. Failed writes retain bytes for retry. Host mounts normally.
 * Export never marks editor assets saved. Do not destroy an active panel from callbacks.
 * @cost Small component/subscription allocation at creation; preparation copies source, writing performs native I/O
 * only when the corresponding button is activated. Status refresh is constant plus diagnostic text.
 */
[[nodiscard]] EVENGINE_API_EDITORS Result<std::unique_ptr<ui::Component>> createSpawnExportPanel(
    std::shared_ptr<SpawnForm> form, filesystem::AtomicTextWriteLease filesystem, std::string initialRelativePath);
}  // namespace eve::spawn_editor
