#pragma once
/** @file SpawnAssetSavePanel.h
 * @brief spawn_editor UI face: explicit asset save action and unsaved/error presentation.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::ui {
class Component;
}
namespace eve::spawn_editor {
class SpawnAssetDocument;
/** @brief Create mountable save controls for an actual asset document; source/history panels stay independently usable.
 * @return Owning component or missing-document error; performs no save or mounting at creation.
 * @ownership Component and callbacks share the actual asset. Old callbacks reject changed source/display identities;
 * callbacks surviving component destruction cannot write the store. No runtime world is referenced.
 * @thread UI/authoring owner thread; callbacks run without locks, reject reentry, and must not destroy an active panel.
 * @remarks Source changes schedule a rebuild. Host must markDirty after saving through a separate external action.
 * Unapplied form drafts are not saved; only committed canonical source is persisted by SpawnAssetDocument.
 * @cost Small owner/subscription allocation at creation; constant status build plus owning diagnostic text per refresh.
 */
[[nodiscard]] EVENGINE_API_EDITORS Result<std::unique_ptr<ui::Component>> createSpawnAssetSavePanel(
    std::shared_ptr<SpawnAssetDocument> document);
}  // namespace eve::spawn_editor
