#pragma once

// Capability interface: scene inspection for devtools / MCP.
//
// Scene is an optional module. DevTools must not include scene headers, so the
// scene module registers an implementation of this interface and devtools
// queries it (nullptr when the module was trimmed out).

#include "common/Export.h"

#include <string>
#include <vector>

namespace eve {

/** @brief Serializable snapshot of one scene node (value type, no scene types). */
struct EVENGINE_API SceneNodeInfo {
    std::string id;
    std::string name;
    std::string path;
    bool visible = true;
    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;
    std::string parent;
    std::vector<std::string> children;
};

/** @brief Scene graph query/mutation surface (provided by the scene module). */
class EVENGINE_API ISceneQuery {
public:
    static constexpr const char* capabilityName = "ISceneQuery";

    virtual ~ISceneQuery() = default;

    /** @brief Name of the currently selected host, or "" when none. */
    virtual std::string activeHost() const = 0;
    /** @brief Number of mounted hosts. */
    virtual int hostCount() const = 0;
    /** @brief Name of the host at `index`. */
    virtual std::string hostNameAt(int index) const = 0;

    /** @brief Node count of the active host (0 when none). */
    virtual int nodeCount() const = 0;
    /** @brief Root node id of the active host, or "". */
    virtual std::string rootId() const = 0;
    /** @brief Depth-first node list of the active host, capped at `limit`. */
    virtual std::vector<SceneNodeInfo> nodes(int limit) const = 0;
    /** @brief Node list of a named host, capped at `limit`. */
    virtual std::vector<SceneNodeInfo> nodesOf(const std::string& host, int limit) const = 0;
    /** @brief Look up one node in the active host. */
    virtual bool getNode(const std::string& id, SceneNodeInfo* out) const = 0;
    /** @brief Look up one node in a named host. */
    virtual bool getNodeIn(const std::string& host, const std::string& id,
                           SceneNodeInfo* out) const = 0;

    /** @brief Mutate the active host's node transform (marks subtree dirty). */
    virtual bool setNodeTransform(const std::string& id, float x, float y, float z) = 0;
    /** @brief Show/hide a node in the active host. */
    virtual bool setNodeVisible(const std::string& id, bool visible) = 0;
    /** @brief Propagate local transforms -> world for every host. */
    virtual void syncTransforms() = 0;
};

}  // namespace eve
