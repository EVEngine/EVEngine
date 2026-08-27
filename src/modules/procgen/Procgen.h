#pragma once

#include "common/Module.h"
#include "common/SquirrelOwnership.h"
#include "procgen/core/ProcgenCore.h"
#include "procgen/Biome.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/ParamSchema.h"
#include "procgen/Params.h"
#include "procgen/PointSet.h"
#include "procgen/PointGraph.h"
#include "procgen/ProcgenSystem.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/algorithms/LSystem.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"

#include <string>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
class Mesh;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::procgen {
struct PbrTextureSet;

/** @brief Handle domain for module-owned procedural parameter objects. */
struct ProcgenParamsHandleTag {};
/** @brief Handle domain for module-owned procedural grids. */
struct ProcgenGridHandleTag {};
/** @brief Handle domain for module-owned procedural rebuild contexts. */
struct ProcgenContextHandleTag {};
/** @brief Handle domain for module-owned output specifications. */
struct ProcgenOutputHandleTag {};
/** @brief Handle domain for module-owned point-set intermediates. */
struct ProcgenPointSetHandleTag {};
/** @brief Handle domain for module-owned terrain samplers. */
struct ProcgenTerrainSamplerHandleTag {};
/** @brief Handle domain for module-owned heightmaps. */
struct ProcgenHeightmapHandleTag {};
/** @brief Handle domain for module-owned cloud fields. */
struct ProcgenCloudFieldHandleTag {};
/** @brief Handle domain for module-owned cloud shadow fields. */
struct ProcgenCloudShadowHandleTag {};
/** @brief Handle domain for module-owned PBR material sets. */
struct ProcgenPbrMaterialHandleTag {};
/** @brief Handle domain for module-owned CPU mesh builds. */
struct ProcgenMeshBuildHandleTag {};
/** @brief Handle domain for module-owned generated images. */
struct ProcgenImageHandleTag {};
/** @brief Handle domain for module-owned generated normal images. */
struct ProcgenNormalImageHandleTag {};
/** @brief Handle domain for module-owned spatial data. */
struct ProcgenSpatialDataHandleTag {};
/** @brief Handle domain for module-owned runtime-generation schedulers. */
struct ProcgenRuntimeGenerationHandleTag {};
/** @brief Handle domain for module-owned point graphs. */
struct ProcgenPointGraphHandleTag {};
/** @brief Handle domain for module-owned biome rules. */
struct ProcgenBiomeRulesHandleTag {};
/** @brief Handle domain for module-owned shape grammars. */
struct ProcgenShapeGrammarHandleTag {};
/** @brief Handle domain for module-owned L-system engines. */
struct ProcgenLSystemHandleTag {};
using ProcgenParamsHandleRef = eve::script::RuntimeHandleRef<ProcgenParamsHandleTag>;
using ProcgenGridHandleRef = eve::script::RuntimeHandleRef<ProcgenGridHandleTag>;
using ProcgenContextHandleRef = eve::script::RuntimeHandleRef<ProcgenContextHandleTag>;
using ProcgenOutputHandleRef = eve::script::RuntimeHandleRef<ProcgenOutputHandleTag>;
using ProcgenPointSetHandleRef = eve::script::RuntimeHandleRef<ProcgenPointSetHandleTag>;
using ProcgenTerrainSamplerHandleRef = eve::script::RuntimeHandleRef<ProcgenTerrainSamplerHandleTag>;
using ProcgenHeightmapHandleRef = eve::script::RuntimeHandleRef<ProcgenHeightmapHandleTag>;
using ProcgenCloudFieldHandleRef = eve::script::RuntimeHandleRef<ProcgenCloudFieldHandleTag>;
using ProcgenCloudShadowHandleRef = eve::script::RuntimeHandleRef<ProcgenCloudShadowHandleTag>;
using ProcgenPbrMaterialHandleRef = eve::script::RuntimeHandleRef<ProcgenPbrMaterialHandleTag>;
using ProcgenMeshBuildHandleRef = eve::script::RuntimeHandleRef<ProcgenMeshBuildHandleTag>;
using ProcgenImageHandleRef = eve::script::RuntimeHandleRef<ProcgenImageHandleTag>;
using ProcgenNormalImageHandleRef = eve::script::RuntimeHandleRef<ProcgenNormalImageHandleTag>;
using ProcgenSpatialDataHandleRef = eve::script::RuntimeHandleRef<ProcgenSpatialDataHandleTag>;
using ProcgenRuntimeGenerationHandleRef = eve::script::RuntimeHandleRef<ProcgenRuntimeGenerationHandleTag>;
using ProcgenPointGraphHandleRef = eve::script::RuntimeHandleRef<ProcgenPointGraphHandleTag>;
using ProcgenBiomeRulesHandleRef = eve::script::RuntimeHandleRef<ProcgenBiomeRulesHandleTag>;
using ProcgenShapeGrammarHandleRef = eve::script::RuntimeHandleRef<ProcgenShapeGrammarHandleTag>;
using ProcgenLSystemHandleRef = eve::script::RuntimeHandleRef<ProcgenLSystemHandleTag>;

/**
 * @brief Procedural generation module.
 * Phase A: runtime maps (semantic Grid2D → TileLayer).
 * Phase B: runtime textures (ImageData / Texture).
 * Phase C (partial): mesh recipes (Marching Cubes → MeshBuild / Mesh).
 * Script: `procgen <- eve.Procgen();`
 */
class Procgen : public Module {
public:
    Module_REG(Procgen);
    Procgen();
    ~Procgen() override;

    /**
     * @brief Allocates module-owned generation parameters.
     * @return Generation- and module-epoch-qualified ownership reference.
     */
    [[nodiscard]] static eve::Result<ProcgenParamsHandleRef> newParamsHandle();
    /** @brief Resolves parameters as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<Params> resolve(
        ProcgenParamsHandleRef reference) noexcept;
    /** @brief Releases module-owned parameters. */
    [[nodiscard]] static eve::Result<void> release(ProcgenParamsHandleRef reference);
    /** @brief Reports whether a parameter reference is stale. */
    [[nodiscard]] static bool isStale(ProcgenParamsHandleRef reference) noexcept;
    /** @brief Allocates an owning output specification. */
    [[nodiscard]] eve::Result<ProcgenOutputHandleRef> newOutputHandle();
    /** @brief Resolves an output specification as a borrowed view. */
    [[nodiscard]] eve::script::Borrowed<OutputSpec> resolveOutput(
        ProcgenOutputHandleRef reference) noexcept;
    /** @brief Releases an output specification. */
    [[nodiscard]] eve::Result<void> releaseOutput(ProcgenOutputHandleRef reference);
    /** @brief Reports whether an output specification reference is stale. */
    [[nodiscard]] bool isOutputStale(ProcgenOutputHandleRef reference) const noexcept;
    /** @brief Allocates module-owned grid storage and returns its handle. */
    [[nodiscard]] static eve::Result<ProcgenGridHandleRef> newGridHandle(
        int width, int height);
    /** @brief Resolves a grid as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<Grid2D> resolve(
        ProcgenGridHandleRef reference) noexcept;
    /** @brief Releases module-owned grid storage. */
    [[nodiscard]] static eve::Result<void> release(ProcgenGridHandleRef reference);
    /** @brief Reports whether a grid reference is stale. */
    [[nodiscard]] static bool isStale(ProcgenGridHandleRef reference) noexcept;

    // --- Script-first point pipelines ---
    /** @brief Allocates a point-set intermediate and returns its owner handle. */
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> newPointSetHandle();
    /** @brief Resolves a point set as a borrowed view. */
    [[nodiscard]] eve::script::Borrowed<PointSet> resolvePointSet(
        ProcgenPointSetHandleRef reference) noexcept;
    /** @brief Releases a point-set intermediate. */
    [[nodiscard]] eve::Result<void> releasePointSet(ProcgenPointSetHandleRef reference);
    /** @brief Reports whether a point-set handle is stale. */
    [[nodiscard]] bool isPointSetStale(ProcgenPointSetHandleRef reference) const noexcept;
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> sampleGridHandle(
        int width, int depth, float spacing, uint32_t seed, float jitter);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterHeightHandle(
        ProcgenPointSetHandleRef input, float minHeight, float maxHeight);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterDensityHandle(
        ProcgenPointSetHandleRef input, float minDensity, float maxDensity);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterBoxHandle(
        ProcgenPointSetHandleRef input, float minX, float minY, float minZ, float maxX,
        float maxY, float maxZ, bool invert = false);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterSlopeHandle(
        ProcgenPointSetHandleRef input, float minDegrees, float maxDegrees);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterPolygonHandle(
        ProcgenPointSetHandleRef input, ProcgenPointSetHandleRef polygon, bool invert = false);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterSplineDistanceHandle(
        ProcgenPointSetHandleRef input, ProcgenPointSetHandleRef controlPoints,
        float minDistance, float maxDistance);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> excludeRadiusHandle(
        ProcgenPointSetHandleRef input, float x, float z, float radius);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> jitterPointsHandle(
        ProcgenPointSetHandleRef input, uint32_t seed, float amountX, float amountZ);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> selfPruneHandle(
        ProcgenPointSetHandleRef input, float radius);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> projectToHeightmapHandle(
        ProcgenPointSetHandleRef input, ProcgenHeightmapHandleRef heightmap, float originX,
        float originZ, float cellSize, float heightScale);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> sampleSplineHandle(
        ProcgenPointSetHandleRef controlPoints, float spacing, uint32_t seed,
        float lateralJitter);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> poissonDiskHandle(
        int width, int depth, float radius, uint32_t seed, int maxPoints);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> mergePointsHandle(
        ProcgenPointSetHandleRef first, ProcgenPointSetHandleRef second);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> transformPointsHandle(
        ProcgenPointSetHandleRef input, float translateX, float translateY, float translateZ,
        float yawDegrees, float scaleX, float scaleY, float scaleZ);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterFloatAttributeHandle(
        ProcgenPointSetHandleRef input, const std::string& name, float minValue, float maxValue,
        bool invert);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterStringAttributeHandle(
        ProcgenPointSetHandleRef input, const std::string& name, const std::string& value,
        bool invert);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> densityCullHandle(
        ProcgenPointSetHandleRef input, uint32_t seed, float multiplier);

    // --- Spatial data and composable PCG domains ---
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> pointDataHandle(
        ProcgenPointSetHandleRef points);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> boxVolumeHandle(
        float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> sphereVolumeHandle(
        float x, float y, float z, float radius);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> splineDataHandle(
        ProcgenPointSetHandleRef controlPoints, float radius);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> heightfieldDataHandle(
        ProcgenHeightmapHandleRef heightmap, float originX, float originZ, float cellSize,
        float heightScale);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> unionSpatialHandle(
        ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> intersectSpatialHandle(
        ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right);
    [[nodiscard]] eve::Result<ProcgenSpatialDataHandleRef> differenceSpatialHandle(
        ProcgenSpatialDataHandleRef left, ProcgenSpatialDataHandleRef right);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> sampleSpatialHandle(
        ProcgenSpatialDataHandleRef spatial, float spacing, uint32_t seed, float jitter);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> filterSpatialHandle(
        ProcgenPointSetHandleRef input, ProcgenSpatialDataHandleRef spatial, bool invert);
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> projectToSpatialHandle(
        ProcgenPointSetHandleRef input, ProcgenSpatialDataHandleRef spatial);

    [[nodiscard]] eve::Result<ProcgenRuntimeGenerationHandleRef> newRuntimeGenerationHandle(
        uint32_t worldSeed);
    [[nodiscard]] eve::Result<ProcgenPointGraphHandleRef> newPointGraphHandle();
    [[nodiscard]] eve::Result<ProcgenBiomeRulesHandleRef> newBiomeRulesHandle();
    [[nodiscard]] eve::Result<ProcgenShapeGrammarHandleRef> newShapeGrammarHandle();
    [[nodiscard]] eve::Result<ProcgenLSystemHandleRef> newLSystemHandle();
    [[nodiscard]] eve::script::Borrowed<SpatialData> resolveSpatialData(
        ProcgenSpatialDataHandleRef reference) noexcept;
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> resolveRuntimeGeneration(
        ProcgenRuntimeGenerationHandleRef reference) noexcept;
    [[nodiscard]] eve::script::Borrowed<PointGraph> resolvePointGraph(
        ProcgenPointGraphHandleRef reference) noexcept;
    [[nodiscard]] eve::script::Borrowed<BiomeRules> resolveBiomeRules(
        ProcgenBiomeRulesHandleRef reference) noexcept;
    [[nodiscard]] eve::script::Borrowed<ShapeGrammar> resolveShapeGrammar(
        ProcgenShapeGrammarHandleRef reference) noexcept;
    [[nodiscard]] eve::script::Borrowed<LSystem> resolveLSystem(
        ProcgenLSystemHandleRef reference) noexcept;
    [[nodiscard]] eve::Result<void> release(ProcgenSpatialDataHandleRef reference);
    [[nodiscard]] eve::Result<void> release(ProcgenRuntimeGenerationHandleRef reference);
    [[nodiscard]] eve::Result<void> release(ProcgenPointGraphHandleRef reference);
    [[nodiscard]] eve::Result<void> release(ProcgenBiomeRulesHandleRef reference);
    [[nodiscard]] eve::Result<void> release(ProcgenShapeGrammarHandleRef reference);
    [[nodiscard]] eve::Result<void> release(ProcgenLSystemHandleRef reference);
    [[nodiscard]] bool isStale(ProcgenSpatialDataHandleRef reference) const noexcept;
    [[nodiscard]] bool isStale(ProcgenRuntimeGenerationHandleRef reference) const noexcept;
    [[nodiscard]] bool isStale(ProcgenPointGraphHandleRef reference) const noexcept;
    [[nodiscard]] bool isStale(ProcgenBiomeRulesHandleRef reference) const noexcept;
    [[nodiscard]] bool isStale(ProcgenShapeGrammarHandleRef reference) const noexcept;
    [[nodiscard]] bool isStale(ProcgenLSystemHandleRef reference) const noexcept;

    [[nodiscard]] eve::Result<void> publishInstances(
        const std::string& batchId, ProcgenPointSetHandleRef points,
        const std::string& assetAttribute, const std::string& defaultAsset);
    [[nodiscard]] eve::Result<void> removeInstances(const std::string& batchId);
    [[nodiscard]] eve::Result<void> publishCellInstances(
        const std::string& prefix, const ProcgenCellRequest& request,
        ProcgenPointSetHandleRef points, const std::string& assetAttribute,
        const std::string& defaultAsset);
    [[nodiscard]] eve::Result<void> removeCellInstances(
        const std::string& prefix, const ProcgenCellRequest& request);
    int getPublishedInstanceCount(const std::string& batchId) const;
    int getPublishedCreatedCount(const std::string& batchId) const;
    int getPublishedReusedCount(const std::string& batchId) const;
    int getPublishedRemovedCount(const std::string& batchId) const;
    uint32_t  deriveSeed(uint32_t parent, const std::string& scope) const;

    // --- Atomic script rebuilds ---
    /** @brief Starts a module-owned rebuild context and returns its handle. */
    [[nodiscard]] static eve::Result<ProcgenContextHandleRef> beginSystemHandle(
        const std::string& name, uint32_t seed);
    /** @brief Starts a cached module-owned rebuild context and returns its handle. */
    [[nodiscard]] static eve::Result<ProcgenContextHandleRef> beginCachedSystemHandle(
        const std::string& name, uint32_t seed, const std::string& buildKey);
    /** @brief Resolves a rebuild context as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<ProcgenContext> resolve(
        ProcgenContextHandleRef reference) noexcept;
    /** @brief Releases a module-owned rebuild context. */
    [[nodiscard]] static eve::Result<void> release(ProcgenContextHandleRef reference);
    /** @brief Reports whether a rebuild context reference is stale. */
    [[nodiscard]] static bool isStale(ProcgenContextHandleRef reference) noexcept;
    [[nodiscard]] eve::Result<void> commitSystem(ProcgenContextHandleRef context);
    [[nodiscard]] eve::Result<void> abortSystem(ProcgenContextHandleRef context);
    [[nodiscard]] eve::Result<void> removeSystem(const std::string& name);
    bool            hasSystem(const std::string& name) const;
    uint64_t        getSystemRevision(const std::string& name) const;
    uint32_t        getSystemSeed(const std::string& name) const;
    std::string     getSystemBuildKey(const std::string& name) const;
    int             getSystemOutputCount(const std::string& name) const;
    std::string     getSystemOutputName(const std::string& name, int index) const;
    int             getSystemDebugStageCount(const std::string& name) const;
    std::string     getSystemDebugStageName(const std::string& name, int index) const;
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> getSystemOutputHandle(
        const std::string& name, const std::string& outputName) const;
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> getSystemDebugStageHandle(
        const std::string& name, const std::string& stageName) const;
    [[nodiscard]] eve::Result<ProcgenPointSetHandleRef> getPreviousSystemDebugStageHandle(
        const std::string& name, const std::string& stageName) const;
    /** @brief Revision number of the snapshot retained for hot-reload comparison. */
    uint64_t        getPreviousSystemRevision(const std::string& name) const;
    std::string     getSystemDebugReport(const std::string& name) const;
    /** @brief Human-readable point-count changes between the current and previous commits. */
    std::string     getSystemDebugDiffReport(const std::string& name) const;

    // --- Phase A: maps ---
    [[nodiscard]] eve::Result<ProcgenGridHandleRef> generateHandle(
        const std::string &algorithmId, ProcgenParamsHandleRef params);
    [[nodiscard]] eve::Result<void> generateTo(
        const std::string &algorithmId, ProcgenParamsHandleRef params,
        ProcgenOutputHandleRef output);
    [[nodiscard]] eve::Result<void> applyToLayer(
        ProcgenGridHandleRef grid, const std::string &palette, map::TileLayer& layer);

    void setPaletteGid(const std::string &palette, const std::string &semantic, int gid);
    int  getPaletteGid(const std::string &palette, const std::string &semantic) const;

    int         getAlgorithmCount() const;
    std::string getAlgorithmId(int index) const;
    bool        hasAlgorithm(const std::string &algorithmId) const;
    /** @brief Returns an owning copy of an algorithm schema or a diagnostic. */
    [[nodiscard]] eve::Result<RecipeDescriptor> getAlgorithmSchema(
        const std::string &algorithmId) const;
    /** @brief Human-readable algorithm label from its schema.
     * @param algorithmId Algorithm id. @return Display name or empty text. */
    std::string getAlgorithmDisplayName(const std::string &algorithmId) const;
    /** @brief Algorithm category from its schema.
     * @param algorithmId Algorithm id. @return Category or empty text. */
    std::string getAlgorithmCategory(const std::string &algorithmId) const;
    /** @brief Number of reflected parameters for one algorithm.
     * @param algorithmId Algorithm id. @return Parameter count. */
    int getAlgorithmParamCount(const std::string &algorithmId) const;
    /** @brief Stable parameter key at an index.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Key or empty text. */
    std::string getAlgorithmParamKey(const std::string &algorithmId, int index) const;
    /** @brief Human-readable parameter label at an index.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Label or empty text. */
    std::string getAlgorithmParamLabel(const std::string &algorithmId, int index) const;
    /** @brief Parameter help text at an index.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Help text or empty text. */
    std::string getAlgorithmParamDescription(const std::string &algorithmId, int index) const;
    /** @brief Parameter group at an index.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Group or empty text. */
    std::string getAlgorithmParamCategory(const std::string &algorithmId, int index) const;
    /** @brief Parameter kind as int, float, bool, string or choice.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Kind or empty text. */
    std::string getAlgorithmParamKind(const std::string &algorithmId, int index) const;
    /** @brief Schema default encoded as text for lossless dynamic UI transport.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Encoded default. */
    std::string getAlgorithmParamDefault(const std::string &algorithmId, int index) const;
    /** @brief Return true when a numeric minimum is declared.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Whether it exists. */
    bool algorithmParamHasMinimum(const std::string &algorithmId, int index) const;
    /** @brief Return true when a numeric maximum is declared.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Whether it exists. */
    bool algorithmParamHasMaximum(const std::string &algorithmId, int index) const;
    /** @brief Numeric minimum.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Minimum or zero. */
    float getAlgorithmParamMinimum(const std::string &algorithmId, int index) const;
    /** @brief Numeric maximum.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Maximum or zero. */
    float getAlgorithmParamMaximum(const std::string &algorithmId, int index) const;
    /** @brief Suggested numeric editing step.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Step or zero. */
    float getAlgorithmParamStep(const std::string &algorithmId, int index) const;
    /** @brief Return true when a parameter should be hidden by compact inspectors.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Whether it is advanced. */
    bool isAlgorithmParamAdvanced(const std::string &algorithmId, int index) const;
    /** @brief Number of allowed values for a choice parameter.
     * @param algorithmId Algorithm id. @param index Parameter index. @return Choice count. */
    int getAlgorithmParamChoiceCount(const std::string &algorithmId, int index) const;
    /** @brief Choice value at an index.
     * @param algorithmId Algorithm id. @param paramIndex Parameter index.
     * @param choiceIndex Choice index. @return Choice or empty text. */
    std::string getAlgorithmParamChoice(const std::string &algorithmId, int paramIndex,
                                        int choiceIndex) const;
    /** @brief Fill missing algorithm-specific values from schema defaults.
     * @param algorithmId Algorithm id. @param params Parameters to update.
     * @return Whether the schema exists. */
    [[nodiscard]] eve::Result<void> applyAlgorithmDefaults(
        const std::string &algorithmId, ProcgenParamsHandleRef params) const;

    /**
     * @brief Post-process a generated grid: fill each wall cell's detail with an
     * 8-bit neighbour mask (autotile directions). Mutates `grid` in place.
     */
    [[nodiscard]] eve::Result<void> autotileGrid(ProcgenGridHandleRef grid);

    /** @brief Fresh non-zero seed for regenerating a level. */
    uint32_t randomSeed();

    [[nodiscard]] eve::Result<std::string> gridToJson(ProcgenGridHandleRef grid) const;

    // --- Phase B: textures ---
    [[nodiscard]] eve::Result<ProcgenImageHandleRef> generateImageHandle(
        const std::string &recipeId, ProcgenParamsHandleRef params);
    /**
     * @brief Resolves a generated image as a non-owning observation.
     * @param reference Generation- and module-epoch-qualified image reference.
     * @return A borrowed image, or an unbound observation when the reference is
     *         invalid, stale, or the Procgen module is unavailable.
     * @ownership Borrowed; Procgen retains sole ownership of the image.
     * @nullable Yes; the result is unbound for an invalid or stale reference.
     * @lifetime Valid only until the image is released, the Procgen module is
     *           cleared/unloaded, or the owner mutates the registry.
     * @thread Procgen owner thread; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] static eve::script::Borrowed<image::ImageData> resolve(
        ProcgenImageHandleRef reference) noexcept;
    /**
     * @brief Releases a module-owned generated image.
     * @param reference Generation- and module-epoch-qualified image reference.
     * @return Applied on success, or a structured invalid/stale-handle failure.
     */
    [[nodiscard]] static eve::Result<void> release(ProcgenImageHandleRef reference);
    /**
     * @brief Reports whether a generated image reference can no longer resolve.
     * @param reference Image reference to inspect.
     * @return True for a valid reference whose slot is gone or whose owner epoch
     *         no longer matches; invalid references return false.
     */
    [[nodiscard]] static bool isStale(ProcgenImageHandleRef reference) noexcept;

    [[nodiscard]] eve::Result<ProcgenNormalImageHandleRef> generateNormalImageHandle(
        const std::string &recipeId, ProcgenParamsHandleRef params);
    /**
     * @brief Resolves a generated normal image as a non-owning observation.
     * @param reference Generation- and module-epoch-qualified normal-image reference.
     * @return A borrowed image, or an unbound observation when the reference is
     *         invalid, stale, or the Procgen module is unavailable.
     * @ownership Borrowed; Procgen retains sole ownership of the normal image.
     * @nullable Yes; the result is unbound for an invalid or stale reference.
     * @lifetime Valid only until the normal image is released, the Procgen module
     *           is cleared/unloaded, or the owner mutates the registry.
     * @thread Procgen owner thread; no synchronization is provided.
     * @reentrancy Side-effect free and does not invoke callbacks.
     */
    [[nodiscard]] static eve::script::Borrowed<image::ImageData> resolve(
        ProcgenNormalImageHandleRef reference) noexcept;
    /**
     * @brief Releases a module-owned generated normal image.
     * @param reference Generation- and module-epoch-qualified normal-image reference.
     * @return Applied on success, or a structured invalid/stale-handle failure.
     */
    [[nodiscard]] static eve::Result<void> release(ProcgenNormalImageHandleRef reference);
    /**
     * @brief Reports whether a generated normal-image reference can no longer resolve.
     * @param reference Normal-image reference to inspect.
     * @return True for a valid reference whose slot is gone or whose owner epoch
     *         no longer matches; invalid references return false.
     */
    [[nodiscard]] static bool isStale(ProcgenNormalImageHandleRef reference) noexcept;
    /**
     * @brief Uploads a generated recipe to Graphics and returns a borrowed texture.
     * @param recipeId Registered texture recipe id.
     * @param params Generation parameters owned by Procgen.
     * @param gfx Graphics owner that retains the returned texture.
     * @return A borrowed Graphics-owned texture, or an unbound result when generation or upload
     *         cannot be completed.
     * @ownership Graphics owns the returned texture; Procgen releases its temporary image after
     *             the synchronous upload.
     * @lifetime Valid until the Graphics owner destroys or replaces the texture.
     * @thread Graphics/Procgen owner thread; no synchronization is provided.
     * @reentrancy Does not invoke user callbacks.
     */
    [[nodiscard]] eve::script::Borrowed<graphics::Texture> generateTextureBorrowed(
        const std::string &recipeId, ProcgenParamsHandleRef params,
        graphics::Graphics *gfx);

    int         getTextureRecipeCount() const;
    std::string getTextureRecipeId(int index) const;
    bool        hasTextureRecipe(const std::string &recipeId) const;
    /** @brief Returns an owning copy of a texture schema or a diagnostic. */
    [[nodiscard]] eve::Result<RecipeDescriptor> getTextureRecipeSchema(
        const std::string &recipeId) const;
    /** @brief Fill missing texture parameters in a module-owned parameter object. */
    [[nodiscard]] eve::Result<void> applyTextureRecipeDefaults(
        const std::string &recipeId, ProcgenParamsHandleRef params) const;

    // --- Phase E: dynamic clouds + cloud shadows ---
    /** @brief New deterministic, tiling, time-animated cloud field (caller owns). */
    [[nodiscard]] eve::Result<ProcgenCloudFieldHandleRef> newCloudFieldHandle();
    [[nodiscard]] eve::script::Borrowed<CloudField> resolveCloudField(ProcgenCloudFieldHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releaseCloudField(ProcgenCloudFieldHandleRef);
    [[nodiscard]] bool isCloudFieldStale(ProcgenCloudFieldHandleRef) const noexcept;
    /** @brief New sun projection over a cloud field → ground shadows (caller owns). */
    [[nodiscard]] eve::Result<ProcgenCloudShadowHandleRef> newCloudShadowHandle();
    [[nodiscard]] eve::script::Borrowed<CloudShadow> resolveCloudShadow(ProcgenCloudShadowHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releaseCloudShadow(ProcgenCloudShadowHandleRef);
    [[nodiscard]] bool isCloudShadowStale(ProcgenCloudShadowHandleRef) const noexcept;
    /** @brief Cloud coverage at world (x, z) and time using a live field handle. */
    [[nodiscard]] eve::Result<float> cloudCoverageAt(
        ProcgenCloudFieldHandleRef field, float x, float z, float time);
    /** @brief Light multiplier in [0,1] using a live shadow handle. */
    [[nodiscard]] eve::Result<float> cloudShadowFactor(
        ProcgenCloudShadowHandleRef shadow, float x, float z, float time);
    /** @brief Samples coverage into a caller-owned buffer after validating its size. */
    [[nodiscard]] eve::Result<void> sampleCloud(
        ProcgenCloudFieldHandleRef field, std::span<float> out, int w, int h,
        float time, float x0, float z0, float extent);
    /** @brief Samples shadow coverage into a caller-owned buffer after validating its size. */
    [[nodiscard]] eve::Result<void> sampleCloudShadow(
        ProcgenCloudShadowHandleRef shadow, std::span<float> out, int w, int h,
        float time, float x0, float z0, float extent);
    /**
     * @brief Full metallic-roughness PBR set (albedo/normal/roughness/metallic/height/ao)
     * derived from a single displacement field. Caller owns the returned set
     * (call PbrTextureSet::destroy()). Recipes: pbr.soil/stone/rock/marble/water/
     * ripple/wood/cloth/ornament/spot/zebra/wall/cement/mud/sky_cloud.
     */
    [[nodiscard]] eve::Result<ProcgenPbrMaterialHandleRef> generatePbrMaterialHandle(
        const std::string &recipeId, ProcgenParamsHandleRef params);
    [[nodiscard]] eve::script::Borrowed<PbrTextureSet> resolvePbrMaterial(ProcgenPbrMaterialHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releasePbrMaterial(ProcgenPbrMaterialHandleRef);
    [[nodiscard]] bool isPbrMaterialStale(ProcgenPbrMaterialHandleRef) const noexcept;

    int         getPbrRecipeCount() const;
    std::string getPbrRecipeId(int index) const;
    bool        hasPbrRecipe(const std::string &recipeId) const;
    /** @brief Returns an owning copy of a PBR schema or a diagnostic. */
    [[nodiscard]] eve::Result<RecipeDescriptor> getPbrRecipeSchema(
        const std::string &recipeId) const;
    /** @brief Fill missing PBR parameters in a module-owned parameter object. */
    [[nodiscard]] eve::Result<void> applyPbrRecipeDefaults(
        const std::string &recipeId, ProcgenParamsHandleRef params) const;

    // --- Mesh recipes (Marching Cubes, …) ---
    /**
     * @brief CPU mesh (caller owns).
     * Recipes include mesh.marchingcubes, mesh.hexplanet, mesh.castle and the
     * registered vegetation, building and linear-structure recipes.
     */
    [[nodiscard]] eve::Result<ProcgenMeshBuildHandleRef> buildMeshHandle(
        const std::string &recipeId, ProcgenParamsHandleRef params);
    [[nodiscard]] eve::script::Borrowed<MeshBuild> resolveMeshBuild(ProcgenMeshBuildHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releaseMeshBuild(ProcgenMeshBuildHandleRef);
    [[nodiscard]] bool isMeshBuildStale(ProcgenMeshBuildHandleRef) const noexcept;
    /**
     * @brief Build an owning backend-neutral CPU artifact for a mesh recipe.
     * @param recipeId Registered mesh recipe id.
     * @param params Deterministic generation parameters; must not be null.
     * @param id Non-nil identity for this artifact instance.
     * @return Generated CPU artifact or a structured diagnostic.
     */
    [[nodiscard]] eve::Result<GeneratedArtifact> buildArtifact(
        const std::string &recipeId, ProcgenParamsHandleRef params, ArtifactId id);
    /**
     * @brief Build then transactionally publish through requested optional adapters.
     * @param recipeId Registered mesh recipe id.
     * @param params Deterministic generation parameters; must not be null.
     * @param id Non-nil identity for this artifact instance.
     * @param options Scene, graphics and physics adapters required by this call.
     * @return Publication receipt or an Unsupported/validation/adapter diagnostic.
     * @remarks Must run on the main/simulation thread. A failure leaves the store unchanged.
     */
    [[nodiscard]] eve::Result<ArtifactPublishReceipt> publishArtifact(
        const std::string &recipeId, ProcgenParamsHandleRef params, ArtifactId id,
        ArtifactPublishOptions options = {});
    /** @brief Borrow the compatibility facade's published CPU artifact store. */
    [[nodiscard]] const ArtifactStore& artifactStore() const noexcept { return artifactStore_; }
    /** @brief Upload an existing/composed CPU mesh to Graphics. */
    /** @brief Upload a CPU mesh and return a Graphics-owned borrowed mesh. */
    [[nodiscard]] eve::script::Borrowed<graphics::Mesh> uploadMeshBorrowed(
        const MeshBuild& mesh, graphics::Graphics& gfx);
    /** @brief Build + upload to GPU Mesh (owned by Graphics). */
    [[nodiscard]] eve::script::Borrowed<graphics::Mesh> generateMeshBorrowed(
        const std::string &recipeId, ProcgenParamsHandleRef params,
        graphics::Graphics *gfx);

    int         getMeshRecipeCount() const;
    std::string getMeshRecipeId(int index) const;
    bool        hasMeshRecipe(const std::string &recipeId) const;
    /** @brief Returns an owning copy of a mesh schema or a diagnostic. */
    [[nodiscard]] eve::Result<RecipeDescriptor> getMeshRecipeSchema(
        const std::string &recipeId) const;
    /** @brief Fill missing mesh parameters in a module-owned parameter object. */
    [[nodiscard]] eve::Result<void> applyMeshRecipeDefaults(
        const std::string &recipeId, ProcgenParamsHandleRef params) const;

    // --- Phase D: terrain height sampling ---
    /** @brief Sampling function over continuous map coordinates (caller owns). */
    [[nodiscard]] eve::Result<ProcgenTerrainSamplerHandleRef> newTerrainSamplerHandle();
    [[nodiscard]] eve::script::Borrowed<TerrainSampler> resolveTerrainSampler(ProcgenTerrainSamplerHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releaseTerrainSampler(ProcgenTerrainSamplerHandleRef);
    [[nodiscard]] bool isTerrainSamplerStale(ProcgenTerrainSamplerHandleRef) const noexcept;
    /** @brief Empty in-memory heightmap (caller owns). */
    [[nodiscard]] eve::Result<ProcgenHeightmapHandleRef> newHeightmapHandle(int width, int height);
    [[nodiscard]] eve::script::Borrowed<Heightmap> resolveHeightmap(ProcgenHeightmapHandleRef) noexcept;
    [[nodiscard]] eve::Result<void> releaseHeightmap(ProcgenHeightmapHandleRef);
    [[nodiscard]] bool isHeightmapStale(ProcgenHeightmapHandleRef) const noexcept;
    /** @brief Build a sampler from params (seed/scale/octaves/…) and materialize it (caller owns). */
    [[nodiscard]] eve::Result<ProcgenHeightmapHandleRef> generateHeightmapHandle(
        ProcgenParamsHandleRef params);
    /** @brief Classify a heightmap into a module-owned grid using params bands. */
    [[nodiscard]] eve::Result<ProcgenGridHandleRef> heightmapToGrid(
        ProcgenHeightmapHandleRef heightmap, ProcgenParamsHandleRef params);

    PaletteTable &palettes() { return palettes_; }

private:
    bool runGenerate(const std::string &algorithmId, const Params &params, Grid2D &out);
    ArtifactId nextCompatibilityArtifactId() noexcept;

    struct OwnershipState;
    std::unique_ptr<OwnershipState> ownership_;

    PaletteTable                     palettes_;
    mutable std::string              lastError_;
    mutable std::vector<std::string> algorithmIdsCache_;
    mutable std::vector<std::string> textureRecipeIdsCache_;
    mutable std::vector<std::string> pbrRecipeIdsCache_;
    mutable std::vector<std::string> meshRecipeIdsCache_;
    std::unordered_map<std::string, ProcgenSystemSnapshot> systems_;
    std::unordered_map<std::string, ProcgenSystemSnapshot> previousSystems_;
    ArtifactStore artifactStore_;
    std::uint64_t nextArtifactSequence_ = 1;
    eve::script::RuntimeObjectRegistry<Params, ProcgenParamsHandleTag> params_;
    eve::script::RuntimeObjectRegistry<Grid2D, ProcgenGridHandleTag> grids_;
    eve::script::RuntimeObjectRegistry<ProcgenContext, ProcgenContextHandleTag> contexts_;
    eve::script::RuntimeObjectRegistry<SpatialData, ProcgenSpatialDataHandleTag> spatialData_;
    eve::script::RuntimeObjectRegistry<RuntimeGeneration, ProcgenRuntimeGenerationHandleTag> runtimeGenerations_;
    eve::script::RuntimeObjectRegistry<PointGraph, ProcgenPointGraphHandleTag> pointGraphs_;
    eve::script::RuntimeObjectRegistry<BiomeRules, ProcgenBiomeRulesHandleTag> biomeRules_;
    eve::script::RuntimeObjectRegistry<ShapeGrammar, ProcgenShapeGrammarHandleTag> shapeGrammars_;
    eve::script::RuntimeObjectRegistry<LSystem, ProcgenLSystemHandleTag> lsystems_;
};

}  // namespace eve::procgen
