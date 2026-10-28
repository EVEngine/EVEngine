#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"

#include <memory>
#include <string>

namespace eve::physics {

class World3D;

/**
 * @brief Optional physics satellite for Chaos-style geometry-collection destruction.
 *
 * Owns no instances itself; factories transfer ownership to the caller/script VM.
 */
class EVENGINE_API_DOMAINS Destruction final : public Module {
public:
    Module_REG(Destruction);

    /**
     * @brief Registers asset `physics:geometry-collection@2` and instance snapshot
     * `physics:geometry-collection-instance@1`.
     */
    [[nodiscard("check destruction schema registration")]] eve::Result<void> registerGeometryCollectionSchema();

    /** @brief Decode a versioned asset document. */
    [[nodiscard("check geometry-collection decode")]]
    eve::Result<GeometryCollectionAsset> assetFromJson(const std::string& json);

    /** @brief Build the two-box welded fixture used by demos and tests. */
    [[nodiscard("check welded-boxes fixture")]]
    eve::Result<GeometryCollectionAsset> makeWeldedBoxesFixture(float strainThreshold = 1.f);

    /** @brief Build the two-cluster anchored pillar fixture used by demos. */
    [[nodiscard("check cluster-pillar fixture")]]
    eve::Result<GeometryCollectionAsset> makeClusterPillarFixture(float interClusterStrain = 0.8f);

    /**
     * @brief Instantiate a collection against a live World3D.
     * @ownership Caller owns the returned instance.
     */
    [[nodiscard("check geometry-collection create")]]
    eve::Result<std::unique_ptr<GeometryCollectionInstance>> createInstance(World3D* world,
                                                                            const GeometryCollectionAsset& asset,
                                                                            float originX, float originY,
                                                                            float originZ);

    /** @brief Script facade: welded boxes fixture or throw. */
    GeometryCollectionAsset* newWeldedBoxesFixtureScript(float strainThreshold);
    /** @brief Script facade: cluster pillar fixture or throw. */
    GeometryCollectionAsset* newClusterPillarFixtureScript(float interClusterStrain);
    /** @brief Script facade: decode asset JSON or throw; VM owns the pointer. */
    GeometryCollectionAsset* assetFromJsonScript(const std::string& json);
    /** @brief Script facade: create instance or throw; VM owns the pointer. */
    GeometryCollectionInstance* createInstanceScript(World3D* world, GeometryCollectionAsset* asset, float originX,
                                                     float originY, float originZ);
};

}  // namespace eve::physics
