#pragma once

#include "common/ECS.h"

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
    };

    struct ScriptBindings {
        std::vector<HSQOBJECT> instances;  // rooted script entity instances
    };

    COMPONENT(Meta, meta)
    COMPONENT(ScriptBindings, scriptBindings)

    static SceneObject *createObject(const std::string &hostName,
                                     const std::string &nodeId);
};

}  // namespace eve::scene
