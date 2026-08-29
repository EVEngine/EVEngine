#pragma once

#include "editor/EditorAssetDatabase.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Validated CPU geometry resolved from a collider asset. */
struct PhysicsColliderAssetGeometry {
    std::string kind;
    std::vector<float> vertices;
    std::vector<std::int32_t> indices;
    std::vector<float> heights;
    int countX = 0, countZ = 0;
    float cellSizeX = 1.0F, cellSizeZ = 1.0F;
    float minimumHeight = 0.0F, maximumHeight = 0.0F;
    bool loop = false;
};

/** @brief Abstract resolver keeping physics runtime bridges independent of AssetDB storage. */
class IPhysicsColliderAssetResolver {
public:
    virtual ~IPhysicsColliderAssetResolver() = default;
    /** @brief Resolve an immutable collider geometry artifact by GUID or logical URI. */
    virtual EditorResult<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                                const std::string& expectedKind) const = 0;
};

/** @brief AssetDB resolver for 2D polygon/chain and 3D collider metadata. */
class AssetDatabasePhysicsColliderResolver final : public IPhysicsColliderAssetResolver {
public:
    explicit AssetDatabasePhysicsColliderResolver(const MemoryAssetDatabase* database) : database_(database) {}
    EditorResult<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                        const std::string& expectedKind) const override;
private:
    const MemoryAssetDatabase* database_ = nullptr;
};

}  // namespace eve::editor
