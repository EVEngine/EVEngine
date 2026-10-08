#pragma once
#include "common/Export.h"


#include "editor/EditorAssetDatabase.h"
#include "physics/editing/PhysicsColliderAsset.h"

namespace eve::editor {

using PhysicsColliderAssetGeometry  = eve::physics_editing::PhysicsColliderAssetGeometry;
using IPhysicsColliderAssetResolver = eve::physics_editing::IPhysicsColliderAssetResolver;

/** @brief High-level AssetDB adapter for the storage-neutral physics resolver. */
class EVENGINE_API_EDITORS AssetDatabasePhysicsColliderResolver final : public IPhysicsColliderAssetResolver {
public:
    /** @brief Asset database physics collider resolver. */
    explicit AssetDatabasePhysicsColliderResolver(const MemoryAssetDatabase* database) : database_(database) {}

    /** @brief Resolve. */
    [[nodiscard]] Result<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                                     const std::string& expectedKind) const override;

private:
    const MemoryAssetDatabase* database_ = nullptr;
};

}  // namespace eve::editor
