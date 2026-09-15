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
 * Rebuilds all groups into one combined ribbon mesh (per-group LOD + optional
 * cluster cull). Geometric Cards LOD remains owned by `graphics/HairCards`
 * (separate PR). Phase 3 exposes Marschner R/TT/TRT lobe weights and a cheap
 * analytical self-shadow / root AO on the hair shader push constants. Phase 5
 * adds optional guide XPBD/Verlet via `update(dt)` (no physics-module include).
 *
 * Caller owns the instance; Graphics owns Mesh / Shader / Texture.
 *
 * @thread Render-thread affine; not safe to share across threads.
 * @reentrancy `draw` must not re-enter Graphics resource creation.
 */
class GroomInstance {
public:
    explicit GroomInstance(Graphics *gfx);
    ~GroomInstance();

    GroomInstance(const GroomInstance &) = delete;
    GroomInstance &operator=(const GroomInstance &) = delete;

    /**
     * @brief Replace runtime strands from a shared asset (deep copy of groups).
     * @ownership Asset remains owned by the caller; instance stores a copy.
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
    [[nodiscard]] int getForcedLod() const;

    /** @brief Screen-size hint in [0,1] used when forced LOD is disabled. */
    void setScreenSize(float screenSize);
    [[nodiscard]] float getScreenSize() const;

    void setWidthScale(float scale);
    [[nodiscard]] float getWidthScale() const;

    void setSideHint(float x, float y, float z);

    /**
     * @brief Enable/disable CPU cluster frustum filtering before mesh bake.
     * When enabled, call `updateVisibility` with the current view-projection.
     */
    void setClusterCullingEnabled(bool enabled);
    [[nodiscard]] bool isClusterCullingEnabled() const;

    /**
     * @brief Frustum-cull every group's cluster grid and rebuild GPU mesh.
     * @param viewProj16 Column-major 4x4 view-projection (glm::mat4 layout).
     */
    [[nodiscard]] Result<void> updateVisibility(const float *viewProj16);

    /**
     * @brief Marschner-approximate lobe weights (R / TT / TRT) applied to the hair shader.
     * Defaults: 1 / 0.45 / 0.25. Values are clamped to >= 0.
     */
    void setMarschnerLobes(float r, float tt, float trt);
    [[nodiscard]] float getMarschnerR() const;
    [[nodiscard]] float getMarschnerTT() const;
    [[nodiscard]] float getMarschnerTRT() const;

    /**
     * @brief Cheap analytical self-shadow (fiber wrap + along-strand root AO).
     *
     * Not a deep shadow map / transmittance volume — see design doc vs UE
     * groom deep shadows. Strength / bias / rootAo are clamped to [0,1].
     * Defaults: 0.35 / 0.25 / 0.3. Set strength to 0 to disable.
     */
    void setSelfShadow(float strength, float bias, float rootAo);
    [[nodiscard]] float getSelfShadowStrength() const;
    [[nodiscard]] float getSelfShadowBias() const;
    [[nodiscard]] float getRootAoStrength() const;

    /** @brief Rebuild GPU ribbon mesh from all groups (LOD + optional visibility). */
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
    [[nodiscard]] const GuideSimParams &getGuideSimParams() const;

    /**
     * @brief Advance guide simulation by `dt` and rebuild the ribbon mesh.
     * No-op success when simulation is disabled.
     */
    [[nodiscard]] Result<void> update(float dt);

    void draw(const glm::mat4 &model);
    void draw();

    [[nodiscard]] Mesh *getMesh() const;
    [[nodiscard]] Shader *getShader() const;
    [[nodiscard]] Texture *getTexture() const;
    [[nodiscard]] int getCurveCount() const;
    [[nodiscard]] int getPointCount() const;
    [[nodiscard]] size_t getGroupCount() const;
    [[nodiscard]] int getActiveLodIndex() const;
    [[nodiscard]] int getActiveRepresentation() const;
    [[nodiscard]] size_t getClusterCount() const;
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
    [[nodiscard]] const GroomGroup *primaryGroup() const;
    [[nodiscard]] size_t resolveLodIndex(const GroomGroup &group) const;
    [[nodiscard]] StrandsDatas decimatedStrands(const StrandsDatas &src, float curveFraction) const;
    [[nodiscard]] Result<void> rebuildClusterGrids();
    [[nodiscard]] Result<void> ensureDrawResources();
    [[nodiscard]] Result<void> setupGuideSimulation(float guideFraction, InterpolationMode mode);
    void clearGuideSimulation();
    void applyShadingParams();

    Graphics *gfx_ = nullptr;
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
