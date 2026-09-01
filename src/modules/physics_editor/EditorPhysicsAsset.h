#pragma once

#include "editor/EditorAssetDatabase.h"
#include "physics_editing/PhysicsColliderAsset.h"

namespace eve::editor {

using PhysicsColliderAssetGeometry  = eve::physics_editing::PhysicsColliderAssetGeometry;
using IPhysicsColliderAssetResolver = eve::physics_editing::IPhysicsColliderAssetResolver;

/** @brief High-level AssetDB adapter for the storage-neutral physics resolver. */
class AssetDatabasePhysicsColliderResolver final : public IPhysicsColliderAssetResolver {
public:
    explicit AssetDatabasePhysicsColliderResolver(const MemoryAssetDatabase* database) : database_(database) {}

    [[nodiscard]] EditorResult<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                                     const std::string& expectedKind) const override;

private:
    const MemoryAssetDatabase* database_ = nullptr;
};

}  // namespace eve::editor
