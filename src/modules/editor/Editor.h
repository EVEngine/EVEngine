#pragma once

#include "common/Module.h"
#include "editing/EditingExtension.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorResult.h"
#include "editor/EditorTarget.h"

#include <memory>
#include <string>

namespace eve::editor {

class TransformGizmo;
class GizmoManager;
class EditorToolbar;
class EditorInspector;
class EditorDock;
class EditorSession;
class EditorWorkspace;
class ScriptEditorTool;
class ConstantBrushFalloff;
class LinearBrushFalloff;
class SmoothBrushFalloff;
class CircleBrushKernel;
class BoxBrushKernel;
class PaintIntFieldOperation;
class AddScalarFieldOperation;
class FieldBrushTool;
class SphereVolumeBrushKernel;
class BoxVolumeBrushKernel;
class PaintIntVolumeOperation;
class VolumeBrushTool;
class EditorAutomationProvider;
class EditorTargetCoordinator;

/**
 * @brief Editor building blocks — not a shipped 3D/map editor app.
 * Script: `editor <- eve.Editor();`
 *
 * Provides transform gizmos, map brushes, and UI shell helpers so games/tools
 * can assemble their own editors (Love2D-style runtime tooling).
 */
class Editor : public Module {
public:
    Module_REG(Editor);
    Editor();
    ~Editor() override;

    /** @brief Create a transform gizmo. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<TransformGizmo> newGizmo();
    /** @brief Create a gizmo manager. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<GizmoManager> newGizmoManager();

    /** @brief Create a toolbar model. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<EditorToolbar> newToolbar();
    /** @brief Create an inspector model. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<EditorInspector> newInspector();
    /** @brief Create a dock model. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<EditorDock> newDock();
    /**
     * @brief Create a host for interchangeable IEditorTool implementations.
     * @ownership Unique ownership transfers to the caller. The session borrows this editor's command service.
     */
    [[nodiscard]] std::unique_ptr<EditorSession> newSession();
    /**
     * @brief Create a UI-neutral composition model for a project-specific editor.
     * @param id Stable workspace identity; empty values are rejected.
     * @param title User-facing title; empty title uses id.
     * @return Applied unique workspace, or Rejected when id is empty.
     * @ownership Success transfers unique ownership to the caller.
     */
    [[nodiscard]] EditorResult<std::unique_ptr<EditorWorkspace>> newWorkspace(const std::string& id,
                                                                              const std::string& title);
    /** @brief Return the UI- and script-neutral command registry owned by this editor module. */
    EditorCommandService& commandService() { return commandService_; }
    /** @brief Return the immutable command registry owned by this editor module. */
    const EditorCommandService& commandService() const { return commandService_; }
    /** @brief Return the open, generation-safe registry used to discover linked domain editing providers. */
    editing::ExtensionProviderRegistry& extensionProviders() { return extensionProviders_; }
    /** @brief Return the immutable domain editing provider registry. */
    const editing::ExtensionProviderRegistry& extensionProviders() const { return extensionProviders_; }
    /**
     * @brief Register a borrowed editable target for UI, script and automation access.
     * @param target Target owned by the caller and kept alive until unregisterEditingTarget().
     * @return Applied, NoOp for the same registration, or structured diagnostics.
     * @thread Owner-thread only.
     */
    [[nodiscard]] EditorResult<void> registerEditingTarget(IEditableTarget& target);
    /**
     * @brief Remove a borrowed editing target and its local transaction history.
     * @return Applied when removed, or NoOp when the target was not registered.
     */
    [[nodiscard]] EditorResult<void> unregisterEditingTarget(const TargetId& target);
    /**
     * @brief Bind a registered target to a host session without exposing the coordinator.
     * @param session Session owned by the caller.
     * @param target Registered target identity.
     * @return Applied or a structured lookup failure.
     */
    [[nodiscard]] EditorResult<void> bindEditingTarget(EditorSession& session, const TargetId& target);
    /**
     * @brief Create a script-backed implementation of the IEditorTool protocol.
     * @ownership Unique ownership transfers to the caller.
     */
    [[nodiscard]] std::unique_ptr<ScriptEditorTool> newScriptTool(const std::string& id, const std::string& label);
    /** @brief Create a constant brush falloff strategy. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<ConstantBrushFalloff> newConstantBrushFalloff();
    /** @brief Create a linear brush falloff strategy. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<LinearBrushFalloff> newLinearBrushFalloff();
    /** @brief Create a smoothstep brush falloff strategy. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<SmoothBrushFalloff> newSmoothBrushFalloff();
    /** @brief Create a circular brush kernel with a replaceable falloff. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<CircleBrushKernel> newCircleBrushKernel();
    /** @brief Create a rotatable box brush kernel with a replaceable falloff. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<BoxBrushKernel> newBoxBrushKernel();
    /** @brief Create an integer-field paint operation. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<PaintIntFieldOperation> newPaintIntFieldOperation(int value);
    /** @brief Create an additive scalar-field operation. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<AddScalarFieldOperation> newAddScalarFieldOperation();
    /**
     * @brief Create a field brush tool whose kernel and operation are supplied separately.
     * @param id Stable project-defined tool id.
     * @param label User-facing label used for transaction names.
     * @return Unconfigured tool; set its kernel and operation before activation.
     * @ownership Unique ownership transfers to the caller.
     */
    [[nodiscard]] std::unique_ptr<FieldBrushTool> newFieldBrushTool(const std::string& id, const std::string& label);
    /** @brief Create a spherical kernel for sparse three-dimensional fields. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<SphereVolumeBrushKernel> newSphereVolumeBrushKernel();
    /** @brief Create an axis-aligned box kernel for sparse three-dimensional fields. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<BoxVolumeBrushKernel> newBoxVolumeBrushKernel();
    /** @brief Create a byte-range integer paint operation for volume targets. @ownership Unique ownership transfers to the caller. */
    [[nodiscard]] std::unique_ptr<PaintIntVolumeOperation> newPaintIntVolumeOperation(int value);
    /**
     * @brief Create a 3D brush tool whose kernel and operation are supplied separately.
     * @ownership Unique ownership transfers to the caller.
     */
    [[nodiscard]] std::unique_ptr<VolumeBrushTool> newVolumeBrushTool(const std::string& id, const std::string& label);

private:
    editing::ExtensionProviderRegistry extensionProviders_;
    EditorCommandService                       commandService_;
    std::unique_ptr<EditorTargetCoordinator>   targets_;
    std::unique_ptr<EditorAutomationProvider>  automation_;
};

}  // namespace eve::editor
