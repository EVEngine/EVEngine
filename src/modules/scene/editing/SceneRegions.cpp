#include "scene/editing/SceneRegions.h"
#include <set>
#include "editing/EditingProperty.h"
#include "editing/EditingValueJson.h"
namespace eve::scene_editing {
editing::Result<std::string> sceneGeometryRevision(const editing::Value& snapshot, const editing::ObjectId& object) {
    using Value         = editing::Value;
    const auto* root    = snapshot.getIf<Value::Object>();
    const auto* sources = root && root->contains("sources") ? root->at("sources").getIf<Value::Object>() : nullptr;
    if (!sources || !sources->contains(object.value()) || !sources->at(object.value()).getIf<Value::Object>())
        return editing::failed<std::string>(editing::Status::NotFound, editing::RuleId("scene.region.source"),
                                            "Region geometry requires an authored source");
    const auto&   source = *sources->at(object.value()).getIf<Value::Object>();
    Value::Object geometry;
    for (const auto* key : {"kind", "seed", "mesh_params", "path", "revision"})
        if (source.contains(key)) geometry[key] = source.at(key);
    return editing::applied<std::string>("eve.scene.geometry-recipe/1:" + editing::editorValueToJson(geometry));
}
editing::Result<editing::Value::Array> sceneVisibilityTargets(const std::vector<SceneRelation>& relations,
                                                              const editing::Value&             snapshot) {
    using Value = editing::Value;
    Value::Array                                  targets;
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& relation : relations) {
        const bool occludes =
            relation.kind == SceneRelationKind::Occludes || relation.kind == SceneRelationKind::ProjectedOverlap;
        if (!occludes && relation.kind != SceneRelationKind::VisibleFraction) continue;
        const auto& object = occludes ? relation.reference : relation.subject;
        if (relation.region) {
            auto revision = sceneGeometryRevision(snapshot, object);
            if (!revision.ok()) return editing::Result<Value::Array>::failure(revision.status());
            if (revision.value() != relation.region->geometryRevision)
                return editing::failed<Value::Array>(
                    editing::Status::Conflict, editing::RuleId("scene.region.stale"),
                    "Region geometry revision changed; explicitly rebind the region before preview");
        }
        if (relation.kind == SceneRelationKind::ProjectedOverlap && seen.emplace(relation.subject.value(), "").second)
            targets.emplace_back(relation.subject.value());
        if (!seen.emplace(object.value(), relation.region ? relation.region->id : "").second) continue;
        if (relation.region)
            targets.emplace_back(Value::Object{{"object", object.value()},
                                               {"region", editing::toEditingValue(relation.region->toValue())}});
        else
            targets.emplace_back(object.value());
    }
    return editing::applied<Value::Array>(std::move(targets));
}
}  // namespace eve::scene_editing
