#pragma once
/** @file SpawnRegionTool.h
 * @brief spawn_editor viewport face: World2D gesture candidates, semantic preview and canonical form transactions.
 */
#include <memory>
#include <string>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "editor/EditorTool.h"
namespace eve::spawn_editor {
class SpawnForm;
/** @brief Region gesture vocabulary; no arbitrary scripts or implicit coordinate conversion. */
enum class RegionShape { Circle, Points, Polygon };
/** @brief World2D region tool owned by the host and registered through the existing EditorSession protocol.
 * @ownership Holds the actual shared form; gesture source/vertices are owning detached candidates. Session borrows
 * the tool and must unregister it before destruction. No domain entities/providers or map pointers are retained.
 * @thread Authoring/UI owner thread; no locks, no mutation reentry during canonical commit. Do not destroy the active
 * tool in callbacks. Host must supply World2D pointer x/y and a World2D overlay sink using the same explicit transform.
 * @remarks Circle is a drag; Points/Polygon collect primary clicks and commit on Enter. Backspace removes the last
 * vertex, Escape/cancel/deactivation discard the gesture. Geometry validation remains solely in compileMap.
 */
class EVENGINE_API_EDITORS SpawnRegionTool final : public editor::IEditorTool {
public:
    /** @brief Release local preview and form ownership; does not clear published units or source. */
    ~SpawnRegionTool();
    SpawnRegionTool(const SpawnRegionTool&)            = delete;
    SpawnRegionTool& operator=(const SpawnRegionTool&) = delete;
    /** @brief Bind an actual sidecar form and stable instance id; pointer coordinates must already be World2D.
     * @return Owning tool or missing form/instance/wrong-document error; does not register or mutate the source.
     * @cost Full source snapshot and instance lookup once per tool creation; no per-frame source parsing.
     */
    [[nodiscard]] static Result<std::unique_ptr<SpawnRegionTool>> create(std::shared_ptr<SpawnForm> form,
                                                                         std::string instanceId, RegionShape shape);
    /** @brief Borrow fixed tool metadata until tool destruction. */
    [[nodiscard]] const editor::ToolDescriptor& descriptor() const override;
    /** @brief Collect World2D samples; circle release plans and commits one canonical source operation.
     * @cost Source snapshot at gesture start and full document transaction at release; moves only update preview.
     */
    editor::ToolResponse pointerEvent(editor::EditorContext& context, const editor::EditorPointerEvent& event) override;
    /** @brief Enter commits points/polygon, Backspace edits preview, Escape cancels without publication. */
    editor::ToolResponse keyEvent(editor::EditorContext& context, const editor::EditorKeyEvent& event) override;
    /** @brief Emit World2D semantic feedback from local samples; no map/source lookup or mutation.
     * @cost Linear in gesture samples per overlay frame; constant for a circle.
     */
    void drawOverlay(editor::EditorContext& context, editor::IEditorOverlay& overlay) override;
    /** @brief Discard an idle gesture; mutation callbacks cannot cancel an active commit. */
    void cancel(editor::EditorContext& context) override;
    /** @brief Discard preview when this tool stops being active. */
    void deactivate(editor::EditorContext& context) override;
    /** @brief Commit the current preview only against its exact captured form revision.
     * @return Canonical geometry/content/transaction failure or success; failures retain preview for correction.
     * @cost Full owning region build and document transaction per explicit commit.
     */
    [[nodiscard]] Result<void> commitRegion();
    /** @brief Copy the latest pointer/key interaction diagnostics for host presentation.
     * @cost Owning diagnostic copy per host refresh, amortized per attempted edit.
     */
    [[nodiscard]] std::vector<Diagnostic> diagnostics() const;

private:
    struct Impl;
    explicit SpawnRegionTool(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn_editor
