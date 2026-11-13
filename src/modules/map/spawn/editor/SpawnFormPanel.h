#pragma once
/** @file SpawnFormPanel.h
 * @brief spawn_editor UI face: canonical property controls, history actions and visible write diagnostics.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
#include "ui/Component.h"
namespace eve::spawn_editor {
class SpawnForm;
/** @brief Mountable form component exposing its own presentation draft fact to the owning workspace.
 * @ownership The panel owns its UI draft; the form remains the sole committed source/history owner.
 * @thread Authoring owner thread. Borrowed references expire at panel destruction; no callback or source mutation.
 */
class EVENGINE_API_EDITORS SpawnFormPanel : public ui::Component {
public:
    /** @brief Release panel-owned UI state through the concrete implementation. */
    ~SpawnFormPanel() override;
    /** @brief True while an explicit structure draft exists, including an unchanged or invalid candidate.
     * @remarks This fact does not infer cleanliness from the committed document revision.
     */
    [[nodiscard]] virtual bool hasStructureDraft() const noexcept = 0;
};
/** @brief Create a mountable UI component for an actual owning Spawn form.
 * @return Owning UI component or InvalidArgument for a missing form; does not mount or initialize rendering.
 * @ownership Panel and generated widget callbacks share the actual form owner. Trees remain memory-safe after panel
 * destruction; stale-generation callbacks reject rather than overwrite subsequent edits. No fake shared ownership.
 * @thread UI/authoring owner thread; callbacks run without locks. Do not destroy the active component during build.
 * @remarks Uses existing UI PropertyView and canonical form transactions. Invalid writes remain visible as diagnostic
 * text; history/retry/discard actions use the real backend. Host attaches the returned Component by normal UI APIs.
 * @cost Small component allocation at creation; build traverses the full form and nested values once per dirty batch.
 */
[[nodiscard]] EVENGINE_API_EDITORS Result<std::unique_ptr<SpawnFormPanel>> createSpawnFormPanel(
    std::shared_ptr<SpawnForm> form);
}  // namespace eve::spawn_editor
