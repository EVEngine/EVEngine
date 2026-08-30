#pragma once

#include "common/Module.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorTarget.h"

#include <memory>
#include <string>

namespace eve {
namespace graphics {
class Graphics;
class Mesh;
}  // namespace graphics
#ifdef EVENGINE_HAS_PROCGEN
namespace procgen {
class Heightmap;
}
#endif
#ifdef EVENGINE_HAS_MAP
namespace map {
class TileLayer;
}
#endif
#ifdef EVENGINE_HAS_VOXEL
namespace voxel {
class VoxelWorld;
}
#endif
}  // namespace eve

namespace eve::editor {

class TransformGizmo;
class GizmoManager;
class TileBuffer;
class Brush;
class EditorToolbar;
class EditorInspector;
class EditorDock;
class EditorHistory;
class LevelDocument;
class LevelFormatRegistry;
class EditorSession;
class EditorWorkspace;
class TileBufferTarget;
#ifdef EVENGINE_HAS_MAP
class TileLayerTarget;
#endif
class HeightmapTarget;
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
#ifdef EVENGINE_HAS_VOXEL
class VoxelWorldTarget;
#endif
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

    TransformGizmo* newGizmo();
    GizmoManager*   newGizmoManager();

    TileBuffer* newTileBuffer(int width, int height);
    Brush*      newBrush();

    EditorToolbar*   newToolbar();
    EditorInspector* newInspector();
    EditorDock*      newDock();
    EditorHistory*   newHistory();
    /** @brief Create a format-neutral top-down level document. */
    LevelDocument* newLevel(int width, int height, float tileWidth, float tileHeight);
    /** @brief Create a registry with native EVEngine and Tiled JSON formats. */
    LevelFormatRegistry* newLevelFormats();
    /** @brief Create a host for interchangeable IEditorTool implementations. */
    EditorSession* newSession();
    /** @brief Create a UI-neutral composition model for a project-specific editor. */
    EditorWorkspace* newWorkspace(const std::string& id, const std::string& title);
    /** @brief Return the UI- and script-neutral command registry owned by this editor module. */
    EditorCommandService& commandService() { return commandService_; }
    /** @brief Return the immutable command registry owned by this editor module. */
    const EditorCommandService& commandService() const { return commandService_; }
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
    /** @brief Adapt existing fields to capability-based editor targets. */
    TileBufferTarget* newTileBufferTarget(const std::string& id, TileBuffer* buffer);
#ifdef EVENGINE_HAS_MAP
    /** @brief Adapt a live map layer to the editor command/undo/brush protocol. */
    TileLayerTarget* newTileLayerTarget(const std::string& id, map::TileLayer* layer);
#endif
    /** @brief Create a script-backed implementation of the IEditorTool protocol. */
    ScriptEditorTool* newScriptTool(const std::string& id, const std::string& label);
    /** @brief Create a constant brush falloff strategy. */
    ConstantBrushFalloff* newConstantBrushFalloff();
    /** @brief Create a linear brush falloff strategy. */
    LinearBrushFalloff* newLinearBrushFalloff();
    /** @brief Create a smoothstep brush falloff strategy. */
    SmoothBrushFalloff* newSmoothBrushFalloff();
    /** @brief Create a circular brush kernel with a replaceable falloff. */
    CircleBrushKernel* newCircleBrushKernel();
    /** @brief Create a rotatable box brush kernel with a replaceable falloff. */
    BoxBrushKernel* newBoxBrushKernel();
    /** @brief Create an integer-field paint operation. */
    PaintIntFieldOperation* newPaintIntFieldOperation(int value);
    /** @brief Create an additive scalar-field operation. */
    AddScalarFieldOperation* newAddScalarFieldOperation();
    /**
     * @brief Create a field brush tool whose kernel and operation are supplied separately.
     * @param id Stable project-defined tool id.
     * @param label User-facing label used for transaction names.
     * @return Unconfigured tool; set its kernel and operation before activation.
     */
    FieldBrushTool* newFieldBrushTool(const std::string& id, const std::string& label);
    /** @brief Create a spherical kernel for sparse three-dimensional fields. */
    SphereVolumeBrushKernel* newSphereVolumeBrushKernel();
    /** @brief Create an axis-aligned box kernel for sparse three-dimensional fields. */
    BoxVolumeBrushKernel* newBoxVolumeBrushKernel();
    /** @brief Create a byte-range integer paint operation for volume targets. */
    PaintIntVolumeOperation* newPaintIntVolumeOperation(int value);
    /** @brief Create a 3D brush tool whose kernel and operation are supplied separately. */
    VolumeBrushTool* newVolumeBrushTool(const std::string& id, const std::string& label);
#ifdef EVENGINE_HAS_VOXEL
    /**
     * @brief Adapt a live voxel world to the generic sparse volume editing protocol.
     * @param id Stable project-defined target id.
     * @param world Non-owning live voxel world pointer.
     * @return New adapter owned by the caller.
     */
    VoxelWorldTarget* newVoxelWorldTarget(const std::string& id, voxel::VoxelWorld* world);
#endif
#ifdef EVENGINE_HAS_PROCGEN
    /**
     * @brief Adapt a live heightmap to the generic scalar-field editing protocol.
     * @param id Stable project-defined target id.
     * @param heightmap Non-owning live heightmap pointer.
     * @return New adapter owned by the caller.
     */
    HeightmapTarget* newHeightmapTarget(const std::string& id, procgen::Heightmap* heightmap);

    /**
     * @brief Apply a circular linear-falloff brush to a heightmap in one native call.
     * @param hm Heightmap to edit.
     * @param centerX Brush center in heightmap-cell coordinates.
     * @param centerY Brush center in heightmap-cell coordinates.
     * @param radius Radius in cells; values below zero are rejected.
     * @param strength Signed center-height delta (positive raises, negative lowers).
     * @return Number of heightmap samples changed.
     */
    int applyHeightmapBrush(procgen::Heightmap* hm, float centerX, float centerY, float radius, float strength);

    /**
     * Build a flat-shaded terrain mesh from a heightmap (grid of cells,
     * X/Z in world units = index * cellSize, Y = height * heightScale).
     * Caller attaches it to a Renderable3D via setMesh(). Returns nullptr on
     * failure (no Graphics module / null heightmap).
     */
    graphics::Mesh* newHeightmapMesh(procgen::Heightmap* hm, float cellSize, float heightScale);
    /** In-place vertex update after heightmap edits (reuses GPU buffers). */
    bool updateHeightmapMesh(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm, float cellSize,
                             float heightScale);

    /**
     * @brief Heightmap mesh with smooth per-vertex normals (gradient of the
     * height field), so bowls/craters shade continuously instead of showing
     * flat-shaded triangle facets. Uses one shared vertex per heightmap sample
     * and a static indexed topology, reducing update bandwidth substantially.
     */
    graphics::Mesh* newHeightmapMeshSmooth(procgen::Heightmap* hm, float cellSize, float heightScale);
    /** @brief In-place update of a smooth-normal heightmap mesh. */
    bool updateHeightmapMeshSmooth(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm,
                                   float cellSize, float heightScale);
#endif

private:
    EditorCommandService                       commandService_;
    std::unique_ptr<EditorTargetCoordinator>   targets_;
    std::unique_ptr<EditorAutomationProvider>  automation_;
};

}  // namespace eve::editor
