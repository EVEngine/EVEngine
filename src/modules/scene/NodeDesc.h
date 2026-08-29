#pragma once

#include "common/Identity.h"

#include <string>
#include <utility>
#include <vector>

namespace eve::scene {

class SceneHost;

/**
 * @brief Declarative scene-node description (build once / on dirty → flatten into SceneHost::Tree).
 * Isomorphic to eve::ui::WidgetDesc.
 */
struct NodeDesc {
    std::string id;
    /**
     * @brief Optional persisted object identity.
     *
     * `id` remains the legacy host-local locator used by scripts and
     * reconciliation. This UUID is the identity that may cross a save/load
     * or process boundary; it is never an ECS entity id.
     */
    eve::SceneObjectId persistentId;
    /** @brief Reconciliation key; defaults to id when empty. */
    std::string key;
    std::string name;
    /** @brief "2d" or "3d" (string enum per module convention). */
    std::string space = "3d";
    bool visible = true;
    std::vector<std::string> tags;
    int layer = 0;
    float bminX = 0.f, bminY = 0.f, bminZ = 0.f;
    float bmaxX = 0.f, bmaxY = 0.f, bmaxZ = 0.f;
    bool hasBounds = false;

    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;

    std::vector<NodeDesc> children;

    NodeDesc &withId(std::string v) {
        id = std::move(v);
        return *this;
    }
    /** @brief Assign a stable persisted scene-object UUID. */
    NodeDesc &withPersistentId(eve::SceneObjectId v) {
        persistentId = v;
        return *this;
    }
    NodeDesc &withKey(std::string v) {
        key = std::move(v);
        return *this;
    }
    NodeDesc &withName(std::string v) {
        name = std::move(v);
        return *this;
    }
    NodeDesc &withSpace(std::string v) {
        space = std::move(v);
        return *this;
    }
    NodeDesc &withVisible(bool v) {
        visible = v;
        return *this;
    }
    NodeDesc &withTag(std::string v) {
        tags.push_back(std::move(v));
        return *this;
    }
    NodeDesc &withTags(std::vector<std::string> v) {
        tags = std::move(v);
        return *this;
    }
    NodeDesc &withLayer(int v) {
        layer = v;
        return *this;
    }
    NodeDesc &withBounds(float minX, float minY, float minZ, float maxX, float maxY,
                         float maxZ) {
        bminX = minX;
        bminY = minY;
        bminZ = minZ;
        bmaxX = maxX;
        bmaxY = maxY;
        bmaxZ = maxZ;
        hasBounds = true;
        return *this;
    }
    NodeDesc &withPosition(float px, float py, float pz = 0.f) {
        x = px;
        y = py;
        z = pz;
        return *this;
    }
    NodeDesc &withRotation(float y_, float p = 0.f, float r = 0.f) {
        yaw = y_;
        pitch = p;
        roll = r;
        return *this;
    }
    NodeDesc &withScale(float s) {
        sx = sy = sz = s;
        return *this;
    }
    NodeDesc &withScaleXYZ(float sx_, float sy_, float sz_) {
        sx = sx_;
        sy = sy_;
        sz = sz_;
        return *this;
    }
    NodeDesc &child(NodeDesc c) {
        children.push_back(std::move(c));
        return *this;
    }

    const std::string &reconcileKey() const { return key.empty() ? id : key; }
};

NodeDesc node(std::string id, std::vector<NodeDesc> children = {}, std::string name = "");
NodeDesc group(std::vector<NodeDesc> children = {}, std::string id = "");
/** @brief Conditional: include `child` only when `cond` is true (empty group otherwise). */
NodeDesc when(bool cond, NodeDesc child);
NodeDesc whenElse(bool cond, NodeDesc ifTrue, NodeDesc ifFalse);

void applyTree(SceneHost *host, NodeDesc root);
/** @brief Key-aware patch when structure matches; else full replace. Returns true if full rebuild. */
bool applyTreeReconcile(SceneHost *host, NodeDesc root);

}  // namespace eve::scene
