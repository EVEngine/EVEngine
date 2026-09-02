#pragma once

#include "scene_editor/EditorSceneTarget.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief One stable scene object stored relative to a prefab root. */
struct PrefabObjectRecord {
    ObjectId sourceId;
    ObjectId parentSourceId;
    std::string name;
    SceneTransformValue transform;
    AssetGuid nestedPrefab;
    Revision nestedRevision = 0;
};

/** @brief Runtime-neutral prefab authoring snapshot. */
struct PrefabAssetSnapshot {
    AssetGuid asset;
    Revision revision = 1;
    ObjectId rootSourceId;
    std::vector<PrefabObjectRecord> objects;
};

/** @brief Stable identity mapping returned with an instantiated prefab plan. */
struct PrefabInstancePlan {
    std::string instanceId;
    AssetGuid sourceAsset;
    Revision sourceRevision = 0;
    std::vector<DomainOperation> operations;
};

/** @brief One inspector/hierarchy badge describing an instance override. */
struct PrefabOverrideRecord {
    ObjectId instanceObject;
    ObjectId sourceObject;
    std::string property;
    std::string kind;
};

/** @brief Nested-prefab dependency diagnostics tied to the inspected root revision. */
struct PrefabDependencyReport {
    EditorStatus status = EditorStatus::Failed;
    AssetGuid rootAsset;
    Revision rootRevision = 0;
    std::vector<AssetGuid> dependencyOrder;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Backend-neutral prefab capture, instantiate and override planning. */
class ScenePrefabService {
public:
    using PrefabResolver = std::function<EditorResult<PrefabAssetSnapshot>(const AssetGuid&)>;
    /** @brief Capture one scene subtree as a deterministic prefab asset. */
    EditorResult<PrefabAssetSnapshot> capture(const AssetGuid& asset, const SceneTargetBase& scene,
                                              const ObjectId& root) const;
    /** @brief Plan collision-safe creation of a prefab below one scene parent. */
    EditorResult<PrefabInstancePlan> instantiate(const PrefabAssetSnapshot& prefab,
                                                 const std::string& instanceId,
                                                 const ObjectId& parent,
                                                 const SceneTargetBase& scene) const;
    /** @brief Plan operations reverting one instance to its source prefab revision. */
    EditorResult<std::vector<DomainOperation>> revertOverrides(const PrefabAssetSnapshot& prefab,
                                                               const std::string& instanceId,
                                                               const ObjectId& parent,
                                                               const SceneTargetBase& scene) const;
    /** @brief Produce a new prefab revision from one structurally compatible instance. */
    EditorResult<PrefabAssetSnapshot> applyOverrides(const PrefabAssetSnapshot& prefab,
                                                     const std::string& instanceId,
                                                     const ObjectId& parent,
                                                     const SceneTargetBase& scene) const;
    /** @brief Compare one instance with its source and return stable override badges. */
    EditorResult<std::vector<PrefabOverrideRecord>> inspectOverrides(
        const PrefabAssetSnapshot& prefab, const std::string& instanceId,
        const ObjectId& parent, const SceneTargetBase& scene) const;
    /** @brief Resolve the complete nested dependency graph and diagnose cycles/stale pins. */
    PrefabDependencyReport inspectDependencies(const PrefabAssetSnapshot& prefab,
                                               const PrefabResolver& resolver) const;
    /** @brief Refresh direct nested revision pins and bump the parent only when they changed. */
    EditorResult<PrefabAssetSnapshot> refreshNestedRevisions(const PrefabAssetSnapshot& prefab,
                                                             const PrefabResolver& resolver) const;
    /** @brief Convert a prefab snapshot to deterministic persisted data. */
    EditorValue snapshotValue(const PrefabAssetSnapshot& prefab) const;
    /** @brief Parse and validate a persisted prefab snapshot atomically. */
    EditorResult<PrefabAssetSnapshot> loadSnapshot(const EditorValue& snapshot) const;
};

}  // namespace eve::editor
