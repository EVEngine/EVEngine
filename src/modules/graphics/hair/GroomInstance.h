#pragma once

#include "common/Result.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/Simulation.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {

class Graphics;
class Mesh;
class Shader;
class Texture;

namespace hair {

/**
 * @brief Runtime groom drawable (UE `UGroomComponent` analogue).
 *
 * Rebuilds all groups into one combined GPU mesh (per-group LOD + optional
 * cluster cull). `Representation::Cards` expands strands through
 * `graphics/HairCards` polylines; `Strands` uses ribbon quads. Phase 3 exposes
 * Marschner R/TT/TRT lobe weights and a cheap analytical self-shadow / root AO
 * on the hair shader push constants. Phase 5 adds optional guide XPBD/Verlet
 * via `update(dt)` (no physics-module include).
 *
 * Caller owns the instance; Graphics owns Mesh / Shader / Texture and must outlive it.
 * Width/side/forced-LOD/culling setters copy scalar desired state; equal values do nothing.
 * Screen-size edits compare group LOD selections. Derived geometry publishes once in view
 * preparation, or in explicit draw/rebuild. Getters never prepare resources.
 * Destruction cancels the registered preparation callback.
 *
 * @thread Render-thread affine; not safe to share across threads.
 * @reentrancy `draw` must not re-enter Graphics resource creation.
 */
class EVENGINE_API_BACKENDS GroomInstance {
public:
    /** @brief Groom instance. */
    explicit GroomInstance(Graphics *gfx);
    /** @brief Groom instance. */
    ~GroomInstance();
    GroomInstance(const GroomInstance &)            = delete;
    GroomInstance &operator=(const GroomInstance &) = delete;
    GroomInstance(GroomInstance &&)                 = delete;
    GroomInstance &operator=(GroomInstance &&)      = delete;


    /**
     * @brief Replace runtime strands from a shared asset (deep copy of groups).
     * @ownership Asset remains owned by the caller; instance stores a copy.
     * @return Structured failure preserving the previous asset and published mesh.
     * @cost Deep copy, cluster construction and initial geometry upload scale with strand points;
     * amortize at asset replacement, rather than calling every frame.
     */
    [[nodiscard]] Result<void> setAsset(const GroomAsset &asset);

    /** @brief Bake a procedural plane groom into a single default group. */
    [[nodiscard]] Result<void> bakeProceduralPlane(float sizeX, float sizeZ,
                                                   const ProceduralParams &params = {});

    /** @brief Bake procedural strands on an indexed triangle mesh. */
    [[nodiscard]] Result<void> bakeProceduralMesh(const float *posXYZ, const float *nrmXYZ,
                                                  int vertexCount, const uint32_t *indices,
                                                  int indexCount,
                                                  const ProceduralParams &params = {});

    /** @brief Force LOD index; -1 restores automatic selection. */
    void setForcedLod(int lod);
    /** @brief Returns the forced lod. */
    [[nodiscard]] int getForcedLod() const;

    /** @brief Screen-size hint in [0,1] used when forced LOD is disabled. */
    void setScreenSize(float screenSize);
    /** @brief Returns the screen size. */
    [[nodiscard]] float getScreenSize() const;

    /** @brief Sets the width scale. */
    void setWidthScale(float scale);
    /** @brief Returns the width scale. */
    [[nodiscard]] float getWidthScale() const;

    /** @brief Sets the side hint. */
    void setSideHint(float x, float y, float z);

    /**
     * @brief Enable/disable CPU cluster frustum filtering before mesh bake.
     * When enabled, call `updateVisibility` with the current view-projection.
     */
    void setClusterCullingEnabled(bool enabled);
    /** @brief True when cluster culling enabled. */
    [[nodiscard]] bool isClusterCullingEnabled() const;

    /**
     * @brief Update the explicitly supplied visibility mask; equal masks do not dirty geometry.
     * @cost Linear in tested clusters and visible curves; geometry publishes before the next draw.
     * @details The mask belongs to the caller's current view. Call before each view when drawing
     * multiple views; automatic view preparation does not replace this explicit mask.
     * @param viewProj16 Column-major 4x4 view-projection (glm::mat4 layout).
     */
    [[nodiscard]] Result<void> updateVisibility(const float *viewProj16);

    /**
     * @brief Marschner-approximate lobe weights (R / TT / TRT) applied to the hair shader.
     * Defaults: 1 / 0.45 / 0.25. Values are clamped to >= 0.
     */
    void setMarschnerLobes(float r, float tt, float trt);
    /** @brief Returns the marschner r. */
    [[nodiscard]] float getMarschnerR() const;
    /** @brief Returns the marschner tt. */
    [[nodiscard]] float getMarschnerTT() const;
    /** @brief Returns the marschner trt. */
    [[nodiscard]] float getMarschnerTRT() const;

    /**
     * @brief Cheap analytical self-shadow (fiber wrap + along-strand root AO).
     *
     * Not a deep shadow map / transmittance volume — see design doc vs UE
     * groom deep shadows. Strength / bias / rootAo are clamped to [0,1].
     * Defaults: 0.35 / 0.25 / 0.3. Set strength to 0 to disable.
     */
    void setSelfShadow(float strength, float bias, float rootAo);
    /** @brief Returns the self shadow strength. */
    [[nodiscard]] float getSelfShadowStrength() const;
    /** @brief Returns the self shadow bias. */
    [[nodiscard]] float getSelfShadowBias() const;
    /** @brief Returns the root ao strength. */
    [[nodiscard]] float getRootAoStrength() const;

    /** @brief Explicit synchronous refresh of pending geometry; clean instances are no-ops.
     * @return Structured failure; a failed refresh retains the last published mesh and remains dirty.
     * @cost Linear in selected strand points plus changed mesh upload; normally automatic before drawing.
     * @thread Render thread, outside an active draw; no external callbacks. */
    [[nodiscard]] Result<void> rebuild();

    /**
     * @brief Enable lightweight guide XPBD/Verlet for every group.
     *
     * Uses each group's `guides` when non-empty; otherwise extracts a guide
     * subset from strands (`guideFraction`, default 0.15). Rest strands/guides
     * and kNN weights are captured at enable time. Does not include the
     * physics module — SoftBody3D can bridge later via capability.
     */
    [[nodiscard]] Result<void> enableGuideSimulation(const GuideSimParams &params = {},
                                                     float guideFraction = 0.15f,
                                                     InterpolationMode mode = InterpolationMode::Offset);

    /** @brief Disable guide simulation and restore rest strands on next rebuild. */
    [[nodiscard]] Result<void> disableGuideSimulation();

    [[nodiscard]] bool isGuideSimulationEnabled() const;
    void setGuideSimParams(const GuideSimParams &params);
    /** @brief Returns the guide sim params. */
    [[nodiscard]] const GuideSimParams &getGuideSimParams() const;

    /**
     * @brief Advance guide simulation by `dt`; queue geometry for the next view preparation or draw.
     * @cost Linear in guide/strand points for each simulation step. Geometry uploads coalesce;
     * simulation steps never coalesce or drop elapsed time. Getters return the last published mesh.
     * No-op success when simulation is disabled.
     */
    [[nodiscard]] Result<void> update(float dt);

    void draw(const glm::mat4 &model);
    /** @brief Draws . */
    void draw();

    /** @brief Return the last published visible mesh, or null for a culled/empty groom.
     * @ownership Borrowed from Graphics; geometry updates reuse this facade.
     * @lifetime Until Graphics releases the mesh or shuts down; publication changes its contents.
     * @thread Render owner thread; no preparation or callbacks. */
    [[nodiscard]] Mesh *getMesh() const;
    /** @brief Returns the shader. */
    [[nodiscard]] Shader *getShader() const;
    /** @brief Returns the texture. */
    [[nodiscard]] Texture *getTexture() const;
    /** @brief Returns the curve count. */
    [[nodiscard]] int getCurveCount() const;
    /** @brief Returns the point count. */
    [[nodiscard]] int getPointCount() const;
    /** @brief Returns the group count. */
    [[nodiscard]] size_t getGroupCount() const;
    /** @brief Returns the active lod index. */
    [[nodiscard]] int getActiveLodIndex() const;
    /** @brief Returns the active representation. */
    [[nodiscard]] int getActiveRepresentation() const;
    /** @brief Returns the cluster count. */
    [[nodiscard]] size_t getClusterCount() const;
    /** @brief Returns the visible curve count. */
    [[nodiscard]] int getVisibleCurveCount() const;

private:
    struct GroupCullState {
        ClusterGrid clusters;
        std::vector<uint32_t> visibleCurves;
        bool hasVisibility = false;
    };

    struct GroupSimState {
        GuideSimulator simulator;
        StrandsDatas restStrands;
        StrandsDatas restGuides;
        std::vector<StrandGuideWeights> weights;
        StrandsDatas deformedStrands;
        bool active = false;
    };

    [[nodiscard]] Result<void> bakeFromStrands(StrandsDatas strands, const char *debugName);
    /**
     * @ownership Borrowed pointer into `asset_`; not transferred.
     * @lifetime Valid until the next asset replace/rebuild that mutates groups.
     */
    [[nodiscard]] const GroomGroup *primaryGroup() const;
    [[nodiscard]] size_t resolveLodIndex(const GroomGroup &group) const;
    [[nodiscard]] StrandsDatas decimatedStrands(const StrandsDatas &src, float curveFraction) const;
    [[nodiscard]] Result<void> rebuildClusterGrids();
    [[nodiscard]] Result<void> ensureDrawResources();
    [[nodiscard]] Result<void> setupGuideSimulation(float guideFraction, InterpolationMode mode);
    void clearGuideSimulation();
    void applyShadingParams();

    [[nodiscard]] Result<void>  publishMesh(const std::vector<float> &positions, const std::vector<float> &normals,
                                            const std::vector<float> &uvs, const std::vector<uint32_t> &indices);
    void                        markGeometryDirty() noexcept { geometryDirty_ = true; }
    Graphics *gfx_ = nullptr;
    uint64_t                    preparationToken_ = 0;
    bool                        geometryDirty_    = true;
    bool                        meshVisible_      = false;
    GroomAsset asset_;
    std::vector<GroupCullState> groupCull_;
    std::vector<GroupSimState> groupSim_;
    Mesh *mesh_ = nullptr;
    Shader *shader_ = nullptr;
    Texture *texture_ = nullptr;
    int forcedLod_ = -1;
    int activeLodIndex_ = 0;
    Representation activeRepresentation_ = Representation::None;
    float screenSize_ = 1.f;
    float widthScale_ = 1.f;
    float clusterCellSize_ = 0.15f;
    float marschnerR_ = 1.f;
    float marschnerTT_ = 0.45f;
    float marschnerTRT_ = 0.25f;
    float selfShadowStrength_ = 0.35f;
    float selfShadowBias_ = 0.25f;
    float rootAoStrength_ = 0.3f;
    float guideFraction_ = 0.15f;
    GuideSimParams guideSimParams_{};
    InterpolationMode guideInterpMode_ = InterpolationMode::Offset;
    bool clusterCulling_ = false;
    bool guideSimEnabled_ = false;
    glm::vec3 sideHint_{1.f, 0.f, 0.f};
    glm::mat4 lastModel_{1.f};
};

}  // namespace hair
}  // namespace eve::graphics
