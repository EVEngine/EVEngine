#pragma once

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"

#include <squirrel.h>

#include <string>
#include <vector>

namespace eve::scene {

/**
 * @brief Per-node ECS identity (created lazily when a node needs script bindings or
 * engine components). A node's stable string id (hostName + nodeId) is mirrored
 * here so ECS systems can address scene nodes and vice versa.
 *
 * Meta holds the binding back to the arena node; ScriptBindings roots the
 * script eve.SceneEntity instances attached to that node (sq_addref). The
 * scene module owns the whole lifecycle; do not create standalone.
 */
class SceneObject : public ecs::Entity {
public:
    ENTITY(SceneObject, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    struct Meta {
        SceneObject *entity = nullptr;
        std::string hostName;
        std::string nodeId;
        /** @brief Stable scene-object identity copied from the owning SceneNode. */
        eve::SceneObjectId persistentId;
    };

    struct ScriptBindings {
        std::vector<HSQOBJECT> instances;  // rooted script entity instances
    };

    COMPONENT(Meta, meta)
    COMPONENT(ScriptBindings, scriptBindings)

    /**
     * @brief Creates an ECS-owned companion object with an explicit ownership contract.
     * @param hostName Owning scene host name.
     * @param nodeId Stable node locator within that host.
     * @param persistentId Stable scene-object identity, if available.
     * @return Borrowed ECS object pointer, or a structured creation failure.
     * @ownership The scene ECS table owns the object; callers must not delete it.
     * @lifetime Valid until deferred ECS destruction or table teardown; use the entity handle across frames.
     * @thread Call on the scene ECS thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter structural ECS mutation while using the
     * result.
     * @remarks The scene/ECS table owns the object; the pointer becomes stale
     *          after destruction or a deferred table commit.
     */
    [[nodiscard]] static eve::Result<SceneObject *> createObject(const std::string &hostName, const std::string &nodeId,
                                                                 eve::SceneObjectId persistentId = {});
};

}  // namespace eve::scene
