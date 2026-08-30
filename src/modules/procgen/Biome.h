#pragma once

#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief One weighted asset entry in a procedural biome layer. */
struct BiomeAssetRule {
    std::string asset;
    float       weight   = 1.f;
    float       minScale = 1.f;
    float       maxScale = 1.f;
    bool        randomYaw = true;
};

/** @brief One spatially-defined biome layer and its asset table. */
struct BiomeLayerRule {
    std::string                  name;
    int                          priority = 0;
    float                        density  = 1.f;
    std::shared_ptr<SpatialData> spatial;
    std::vector<BiomeAssetRule>  assets;
};

/**
 * @brief Data-driven biome distribution rules compatible with PointGraph and scene batches.
 *
 * Candidates are sampled from a caller-provided domain, rejected by exclusion
 * domains, assigned to the highest-priority matching layer, then deterministically
 * select a weighted asset and transform from their independent point seed.
 */
class BiomeRules {
public:
    /** @brief Remove layers and exclusions. */
    void clear();
    /** @brief Add a named spatial layer. Names are unique. */
    bool addLayer(const std::string& name, SpatialData* spatial, int priority, float density);
    bool removeLayer(const std::string& name);
    bool hasLayer(const std::string& name) const;
    int  getLayerCount() const;
    std::string getLayerName(int index) const;
    int         getLayerPriority(const std::string& name) const;
    float       getLayerDensity(const std::string& name) const;

    /** @brief Add a weighted asset rule to a layer. */
    bool addAsset(const std::string& layerName, const std::string& asset, float weight,
                  float minScale, float maxScale, bool randomYaw);
    int         getAssetCount(const std::string& layerName) const;
    std::string getAssetName(const std::string& layerName, int index) const;

    /** @brief Add a copied spatial exclusion shared by every layer. */
    bool addExclusion(SpatialData* spatial);
    int  getExclusionCount() const;

    /**
     * @brief Generate attributed biome points.
     * @return Caller-owned PointSet with `biome` and `asset` string attributes.
     */
    PointSet* generate(SpatialData* domain, float spacing, uint32_t seed, float jitter);
    std::string getError() const;
    std::string debugReport() const;

private:
    BiomeLayerRule*       findLayer(const std::string& name);
    const BiomeLayerRule* findLayer(const std::string& name) const;

    std::vector<BiomeLayerRule>            layers_;
    std::vector<std::shared_ptr<SpatialData>> exclusions_;
    std::string                            error_;
    int                                    lastCandidateCount_ = 0;
    int                                    lastOutputCount_    = 0;
};

}  // namespace eve::procgen
