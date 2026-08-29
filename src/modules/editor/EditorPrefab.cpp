#include "editor/EditorPrefab.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> prefabError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

ObjectId instanceObject(const std::string& instanceId, const ObjectId& source) {
    return ObjectId(instanceId + "/" + source.value());
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue transformValue(const SceneTransformValue& value) {
    return EditorValue::Object{{"x", value.x}, {"y", value.y}, {"z", value.z},
                               {"rotationX", value.rotationX}, {"rotationY", value.rotationY},
                               {"rotationZ", value.rotationZ}, {"scaleX", value.scaleX},
                               {"scaleY", value.scaleY}, {"scaleZ", value.scaleZ}};
}

EditorResult<SceneTransformValue> parseTransform(const EditorValue& value) {
    const auto number = [&](const char* key) -> const double* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<double>() : nullptr;
    };
    const double* x = number("x");
    const double* y = number("y");
    const double* z = number("z");
    const double* rx = number("rotationX");
    const double* ry = number("rotationY");
    const double* rz = number("rotationZ");
    const double* sx = number("scaleX");
    const double* sy = number("scaleY");
    const double* sz = number("scaleZ");
    if (!x || !y || !z || !rx || !ry || !rz || !sx || !sy || !sz)
        return prefabError<SceneTransformValue>(EditorStatus::Rejected, "editor.prefab.invalid-transform",
                                                "Prefab transform requires complete numeric TRS fields");
    return EditorResult<SceneTransformValue>::applied({*x, *y, *z, *rx, *ry, *rz, *sx, *sy, *sz});
}

EditorResult<void> validatePrefab(const PrefabAssetSnapshot& prefab) {
    if (prefab.asset.empty() || prefab.rootSourceId.empty() || prefab.objects.empty() || prefab.revision == 0)
        return prefabError<void>(EditorStatus::Rejected, "editor.prefab.invalid-identity",
                                 "Prefab requires asset, revision, root and objects");
    std::set<ObjectId> ids;
    for (const PrefabObjectRecord& object : prefab.objects)
        if (object.sourceId.empty() || !ids.insert(object.sourceId).second)
            return prefabError<void>(EditorStatus::Rejected, "editor.prefab.duplicate-object",
                                     "Prefab object ids must be unique and non-empty");
    for (const PrefabObjectRecord& object : prefab.objects)
        if (object.nestedPrefab.empty() != (object.nestedRevision == 0))
            return prefabError<void>(EditorStatus::Rejected, "editor.prefab.invalid-nested-pin",
                                     "Nested prefab references require both asset and non-zero revision");
    if (!ids.contains(prefab.rootSourceId))
        return prefabError<void>(EditorStatus::Rejected, "editor.prefab.root-not-found",
                                 "Prefab root does not exist");
    for (const PrefabObjectRecord& object : prefab.objects) {
        if (object.sourceId == prefab.rootSourceId && !object.parentSourceId.empty())
            return prefabError<void>(EditorStatus::Rejected, "editor.prefab.root-has-parent",
                                     "Prefab root must not have an internal parent");
        if (object.sourceId != prefab.rootSourceId && !ids.contains(object.parentSourceId))
            return prefabError<void>(EditorStatus::Rejected, "editor.prefab.parent-not-found",
                                     "Prefab object references a missing parent");
        std::set<ObjectId> ancestry;
        const PrefabObjectRecord* cursor = &object;
        while (!cursor->parentSourceId.empty()) {
            if (!ancestry.insert(cursor->sourceId).second)
                return prefabError<void>(EditorStatus::Rejected, "editor.prefab.hierarchy-cycle",
                                         "Prefab hierarchy contains a cycle");
            const auto parent = std::find_if(prefab.objects.begin(), prefab.objects.end(),
                                             [&](const PrefabObjectRecord& candidate) {
                                                 return candidate.sourceId == cursor->parentSourceId;
                                             });
            if (parent == prefab.objects.end()) break;
            cursor = &*parent;
        }
        if (cursor->sourceId != prefab.rootSourceId)
            return prefabError<void>(EditorStatus::Rejected, "editor.prefab.disconnected-object",
                                     "Every prefab object must descend from its root");
    }
    return EditorResult<void>::applied();
}

}  // namespace

EditorResult<PrefabAssetSnapshot> ScenePrefabService::capture(const AssetGuid& asset,
                                                               const SceneTargetBase& scene,
                                                               const ObjectId& root) const {
    if (asset.empty())
        return prefabError<PrefabAssetSnapshot>(EditorStatus::Rejected, "editor.prefab.asset-required",
                                                "Prefab asset identity is required");
    auto rootObject = scene.sceneObject(root);
    if (!rootObject.accepted() || !rootObject.value)
        return prefabError<PrefabAssetSnapshot>(EditorStatus::NotFound, "editor.prefab.root-not-found",
                                                "Prefab capture root does not exist");
    PrefabAssetSnapshot prefab;
    prefab.asset = asset;
    prefab.rootSourceId = root;
    std::vector<ObjectId> pending{root};
    for (size_t index = 0; index < pending.size(); ++index) {
        const ObjectId id = pending[index];
        auto object = scene.sceneObject(id);
        if (!object.accepted() || !object.value)
            return prefabError<PrefabAssetSnapshot>(EditorStatus::Conflict, "editor.prefab.capture-diverged",
                                                    "Scene hierarchy changed during prefab capture");
        prefab.objects.push_back({id, id == root ? ObjectId{} : object.value->parent,
                                  object.value->name, object.value->transform, {}, 0});
        const auto children = scene.sceneChildren(id);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return EditorResult<PrefabAssetSnapshot>::applied(std::move(prefab));
}

EditorResult<PrefabInstancePlan> ScenePrefabService::instantiate(const PrefabAssetSnapshot& prefab,
                                                                 const std::string& instanceId,
                                                                 const ObjectId& parent,
                                                                 const SceneTargetBase& scene) const {
    if (!validatePrefab(prefab).accepted() || instanceId.empty())
        return prefabError<PrefabInstancePlan>(EditorStatus::Rejected, "editor.prefab.invalid-instance",
                                               "Prefab and non-empty instance id are required");
    if (!parent.empty() && !scene.sceneObject(parent).accepted())
        return prefabError<PrefabInstancePlan>(EditorStatus::NotFound, "editor.prefab.scene-parent-not-found",
                                               "Prefab instance parent does not exist");
    PrefabInstancePlan plan;
    plan.instanceId = instanceId;
    plan.sourceAsset = prefab.asset;
    plan.sourceRevision = prefab.revision;
    for (const PrefabObjectRecord& object : prefab.objects) {
        const ObjectId id = instanceObject(instanceId, object.sourceId);
        if (scene.sceneObject(id).accepted())
            return prefabError<PrefabInstancePlan>(EditorStatus::Conflict, "editor.prefab.instance-id-conflict",
                                                   "Prefab instance object already exists: " + id.value());
        CreateSceneObjectRequest request;
        request.id = id;
        request.parent = object.sourceId == prefab.rootSourceId
                             ? parent
                             : instanceObject(instanceId, object.parentSourceId);
        request.name = object.name;
        request.transform = object.transform;
        auto operation = scene.makeCreate(request);
        if (!operation.accepted() || !operation.value) {
            // Parents created earlier in this plan are intentionally not yet in
            // the target, so construct the same validated operation value here.
            DomainOperation create;
            create.type = "scene.object.create.v1";
            create.inverseType = "scene.object.delete.v1";
            create.target = TargetId(scene.targetId());
            EditorValue::Object payload{{"id", request.id.value()}, {"parent", request.parent.value()},
                                        {"name", request.name}, {"transform", transformValue(request.transform)}};
            create.payload = EditorValue(payload);
            create.inverse = EditorValue(std::move(payload));
            create.hasInverse = true;
            create.affectedObjects.push_back({TargetId(scene.targetId()), request.id.value(), 0});
            plan.operations.push_back(std::move(create));
        } else {
            plan.operations.push_back(std::move(*operation.value));
        }
    }
    return EditorResult<PrefabInstancePlan>::applied(std::move(plan));
}

EditorResult<std::vector<DomainOperation>> ScenePrefabService::revertOverrides(
    const PrefabAssetSnapshot& prefab, const std::string& instanceId, const ObjectId& parent,
    const SceneTargetBase& scene) const {
    if (!validatePrefab(prefab).accepted() || instanceId.empty())
        return prefabError<std::vector<DomainOperation>>(EditorStatus::Rejected, "editor.prefab.invalid-instance",
                                                        "Prefab and instance id are required");
    std::vector<DomainOperation> operations;
    for (const PrefabObjectRecord& source : prefab.objects) {
        const ObjectId id = instanceObject(instanceId, source.sourceId);
        auto object = scene.sceneObject(id);
        if (!object.accepted() || !object.value)
            return prefabError<std::vector<DomainOperation>>(EditorStatus::Conflict,
                                                             "editor.prefab.instance-structure-changed",
                                                             "Prefab instance object is missing: " + id.value());
        const ObjectId wantedParent = source.sourceId == prefab.rootSourceId
                                          ? parent
                                          : instanceObject(instanceId, source.parentSourceId);
        if (object.value->parent != wantedParent) {
            auto operation = scene.makeReparent(id, wantedParent);
            if (!operation.value) return prefabError<std::vector<DomainOperation>>(
                EditorStatus::Conflict, "editor.prefab.revert-reparent", "Could not plan prefab reparent revert");
            operations.push_back(std::move(*operation.value));
        }
        if (object.value->name != source.name) {
            auto operation = scene.makeRename(id, source.name);
            if (!operation.value) return prefabError<std::vector<DomainOperation>>(
                EditorStatus::Conflict, "editor.prefab.revert-rename", "Could not plan prefab rename revert");
            operations.push_back(std::move(*operation.value));
        }
        if (object.value->transform != source.transform) {
            auto operation = scene.makeSetTransform(id, source.transform);
            if (!operation.value) return prefabError<std::vector<DomainOperation>>(
                EditorStatus::Conflict, "editor.prefab.revert-transform", "Could not plan prefab transform revert");
            operations.push_back(std::move(*operation.value));
        }
    }
    return EditorResult<std::vector<DomainOperation>>::applied(std::move(operations));
}

EditorResult<PrefabAssetSnapshot> ScenePrefabService::applyOverrides(const PrefabAssetSnapshot& prefab,
                                                                     const std::string& instanceId,
                                                                     const ObjectId& parent,
                                                                     const SceneTargetBase& scene) const {
    if (!validatePrefab(prefab).accepted() || instanceId.empty())
        return prefabError<PrefabAssetSnapshot>(EditorStatus::Rejected, "editor.prefab.invalid-instance",
                                                "Prefab and instance id are required");
    PrefabAssetSnapshot result = prefab;
    ++result.revision;
    std::map<ObjectId, ObjectId> instanceToSource;
    for (const PrefabObjectRecord& source : prefab.objects)
        instanceToSource.emplace(instanceObject(instanceId, source.sourceId), source.sourceId);
    for (PrefabObjectRecord& destination : result.objects) {
        const ObjectId instance = instanceObject(instanceId, destination.sourceId);
        auto object = scene.sceneObject(instance);
        if (!object.accepted() || !object.value)
            return prefabError<PrefabAssetSnapshot>(EditorStatus::Conflict,
                                                    "editor.prefab.instance-structure-changed",
                                                    "Prefab instance object is missing: " + instance.value());
        destination.name = object.value->name;
        destination.transform = object.value->transform;
        if (destination.sourceId == prefab.rootSourceId) {
            if (object.value->parent != parent)
                return prefabError<PrefabAssetSnapshot>(EditorStatus::Conflict,
                                                        "editor.prefab.root-reparented",
                                                        "Prefab root moved outside its instance parent");
            destination.parentSourceId = ObjectId{};
        } else {
            const auto sourceParent = instanceToSource.find(object.value->parent);
            if (sourceParent == instanceToSource.end())
                return prefabError<PrefabAssetSnapshot>(EditorStatus::Conflict,
                                                        "editor.prefab.child-reparented-outside",
                                                        "Prefab child moved outside its instance");
            destination.parentSourceId = sourceParent->second;
        }
    }
    return EditorResult<PrefabAssetSnapshot>::applied(std::move(result));
}

EditorResult<std::vector<PrefabOverrideRecord>> ScenePrefabService::inspectOverrides(
    const PrefabAssetSnapshot& prefab, const std::string& instanceId, const ObjectId& parent,
    const SceneTargetBase& scene) const {
    if (!validatePrefab(prefab).accepted() || instanceId.empty())
        return prefabError<std::vector<PrefabOverrideRecord>>(EditorStatus::Rejected,
            "editor.prefab.invalid-instance", "Prefab and instance id are required");
    std::vector<PrefabOverrideRecord> result;
    for (const PrefabObjectRecord& source : prefab.objects) {
        const ObjectId instance = instanceObject(instanceId, source.sourceId);
        auto object = scene.sceneObject(instance);
        if (!object.value) {
            result.push_back({instance, source.sourceId, "object", "missing"});
            continue;
        }
        const ObjectId expectedParent = source.sourceId == prefab.rootSourceId
                                            ? parent : instanceObject(instanceId, source.parentSourceId);
        if (object.value->parent != expectedParent)
            result.push_back({instance, source.sourceId, "hierarchy.parent", "modified"});
        if (object.value->name != source.name)
            result.push_back({instance, source.sourceId, "object.name", "modified"});
        if (object.value->transform != source.transform)
            result.push_back({instance, source.sourceId, "transform", "modified"});
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.instanceObject == b.instanceObject ? a.property < b.property
                                                     : a.instanceObject < b.instanceObject;
    });
    return EditorResult<std::vector<PrefabOverrideRecord>>::applied(std::move(result));
}

PrefabDependencyReport ScenePrefabService::inspectDependencies(
    const PrefabAssetSnapshot& prefab, const PrefabResolver& resolver) const {
    PrefabDependencyReport report;
    report.rootAsset = prefab.asset; report.rootRevision = prefab.revision;
    if (!resolver || !validatePrefab(prefab).accepted()) {
        report.status = EditorStatus::Rejected;
        report.diagnostics.push_back({RuleId("editor.prefab.invalid-dependency-root"),
                                      DiagnosticSeverity::Error,
                                      "Dependency inspection requires a valid prefab and resolver"});
        return report;
    }
    std::set<AssetGuid> visiting, visited;
    std::function<bool(const PrefabAssetSnapshot&)> visit = [&](const PrefabAssetSnapshot& current) {
        if (visiting.contains(current.asset)) {
            report.diagnostics.push_back({RuleId("editor.prefab.dependency-cycle"), DiagnosticSeverity::Error,
                                          "Nested prefab dependency cycle reaches: " + current.asset.value()});
            return false;
        }
        if (visited.contains(current.asset)) return true;
        visiting.insert(current.asset);
        bool valid = true;
        for (const PrefabObjectRecord& object : current.objects) {
            if (object.nestedPrefab.empty()) continue;
            auto resolved = resolver(object.nestedPrefab);
            if (!resolved.value) {
                report.diagnostics.push_back({RuleId("editor.prefab.dependency-missing"), DiagnosticSeverity::Error,
                                              "Nested prefab cannot be resolved: " + object.nestedPrefab.value()});
                valid = false; continue;
            }
            if (!validatePrefab(*resolved.value).accepted()) {
                report.diagnostics.push_back({RuleId("editor.prefab.dependency-invalid"), DiagnosticSeverity::Error,
                                              "Nested prefab is invalid: " + object.nestedPrefab.value()});
                valid = false; continue;
            }
            if (resolved.value->revision != object.nestedRevision)
                report.diagnostics.push_back({RuleId("editor.prefab.dependency-stale"), DiagnosticSeverity::Warning,
                                              "Nested prefab revision pin is stale: " + object.nestedPrefab.value()});
            if (!visit(*resolved.value)) valid = false;
        }
        visiting.erase(current.asset); visited.insert(current.asset);
        if (current.asset != prefab.asset) report.dependencyOrder.push_back(current.asset);
        return valid;
    };
    const bool valid = visit(prefab);
    report.status = valid ? EditorStatus::Applied : EditorStatus::Rejected;
    return report;
}

EditorResult<PrefabAssetSnapshot> ScenePrefabService::refreshNestedRevisions(
    const PrefabAssetSnapshot& prefab, const PrefabResolver& resolver) const {
    auto dependencies = inspectDependencies(prefab, resolver);
    if (dependencies.status != EditorStatus::Applied)
        return prefabError<PrefabAssetSnapshot>(dependencies.status, "editor.prefab.invalid-dependencies",
                                                "Nested prefab dependencies are missing, invalid or cyclic");
    PrefabAssetSnapshot result = prefab;
    bool changed = false;
    for (PrefabObjectRecord& object : result.objects) {
        if (object.nestedPrefab.empty()) continue;
        auto resolved = resolver(object.nestedPrefab);
        if (!resolved.value)
            return prefabError<PrefabAssetSnapshot>(resolved.status, "editor.prefab.dependency-missing",
                                                    "Nested prefab cannot be resolved");
        if (object.nestedRevision != resolved.value->revision) {
            object.nestedRevision = resolved.value->revision;
            changed = true;
        }
    }
    if (changed) ++result.revision;
    return EditorResult<PrefabAssetSnapshot>::applied(std::move(result));
}

EditorValue ScenePrefabService::snapshotValue(const PrefabAssetSnapshot& prefab) const {
    EditorValue::Array objects;
    for (const PrefabObjectRecord& object : prefab.objects)
        objects.emplace_back(EditorValue::Object{{"sourceId", object.sourceId.value()},
                                                  {"parentSourceId", object.parentSourceId.value()},
                                                  {"name", object.name},
                                                  {"transform", transformValue(object.transform)},
                                                  {"nestedPrefab", object.nestedPrefab.value()},
                                                  {"nestedRevision", static_cast<int64_t>(object.nestedRevision)}});
    return EditorValue::Object{{"schemaVersion", int64_t{2}}, {"asset", prefab.asset.value()},
                               {"revision", static_cast<int64_t>(prefab.revision)},
                               {"rootSourceId", prefab.rootSourceId.value()}, {"objects", std::move(objects)}};
}

EditorResult<PrefabAssetSnapshot> ScenePrefabService::loadSnapshot(const EditorValue& snapshot) const {
    const EditorValue* schemaValue = field(snapshot, "schemaVersion");
    const EditorValue* assetValue = field(snapshot, "asset");
    const EditorValue* revisionValue = field(snapshot, "revision");
    const EditorValue* rootValue = field(snapshot, "rootSourceId");
    const EditorValue* objectsValue = field(snapshot, "objects");
    const auto* schema = schemaValue ? schemaValue->getIf<int64_t>() : nullptr;
    const auto* asset = assetValue ? assetValue->getIf<std::string>() : nullptr;
    const auto* revision = revisionValue ? revisionValue->getIf<int64_t>() : nullptr;
    const auto* root = rootValue ? rootValue->getIf<std::string>() : nullptr;
    const auto* objects = objectsValue ? objectsValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || (*schema != 1 && *schema != 2) || !asset || !revision || *revision <= 0 || !root || !objects)
        return prefabError<PrefabAssetSnapshot>(EditorStatus::Unsupported, "editor.prefab.invalid-snapshot",
                                                "Prefab snapshot schema is invalid or unsupported");
    PrefabAssetSnapshot prefab{AssetGuid(*asset), static_cast<Revision>(*revision), ObjectId(*root), {}};
    for (const EditorValue& value : *objects) {
        const EditorValue* idValue = field(value, "sourceId");
        const EditorValue* parentValue = field(value, "parentSourceId");
        const EditorValue* nameValue = field(value, "name");
        const EditorValue* transform = field(value, "transform");
        const EditorValue* nestedAssetValue = field(value, "nestedPrefab");
        const EditorValue* nestedRevisionValue = field(value, "nestedRevision");
        const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
        const auto* parentId = parentValue ? parentValue->getIf<std::string>() : nullptr;
        const auto* name = nameValue ? nameValue->getIf<std::string>() : nullptr;
        if (!id || !parentId || !name || !transform)
            return prefabError<PrefabAssetSnapshot>(EditorStatus::Rejected, "editor.prefab.invalid-object",
                                                    "Prefab snapshot contains an invalid object");
        auto parsed = parseTransform(*transform);
        if (!parsed.value)
            return prefabError<PrefabAssetSnapshot>(EditorStatus::Rejected, "editor.prefab.invalid-transform",
                                                    "Prefab snapshot contains an invalid transform");
        AssetGuid nestedAsset;
        Revision nestedRevision = 0;
        if (*schema == 2) {
            const auto* nested = nestedAssetValue ? nestedAssetValue->getIf<std::string>() : nullptr;
            const auto* pinned = nestedRevisionValue ? nestedRevisionValue->getIf<int64_t>() : nullptr;
            if (!nested || !pinned || *pinned < 0)
                return prefabError<PrefabAssetSnapshot>(EditorStatus::Rejected, "editor.prefab.invalid-nested-pin",
                                                        "Prefab snapshot contains an invalid nested revision pin");
            nestedAsset = AssetGuid(*nested); nestedRevision = static_cast<Revision>(*pinned);
        }
        prefab.objects.push_back({ObjectId(*id), ObjectId(*parentId), *name, *parsed.value,
                                  nestedAsset, nestedRevision});
    }
    auto valid = validatePrefab(prefab);
    if (!valid.accepted()) {
        EditorResult<PrefabAssetSnapshot> result;
        result.status = valid.status;
        result.diagnostics = std::move(valid.diagnostics);
        return result;
    }
    return EditorResult<PrefabAssetSnapshot>::applied(std::move(prefab));
}

}  // namespace eve::editor
