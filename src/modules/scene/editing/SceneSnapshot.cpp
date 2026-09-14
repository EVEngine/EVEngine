#include "scene/editing/SceneTarget.h"

#include <cmath>
#include <limits>
#include <set>

namespace eve::scene_editing {

EditorResult<std::map<ObjectId, SceneObjectSnapshot>> SceneTargetBase::parseSnapshot(const EditorValue& value) {
    using Objects     = std::map<ObjectId, SceneObjectSnapshot>;
    const auto reject = [](const char* message) {
        return editing::failed<Objects>(EditorStatus::Rejected, RuleId("scene.snapshot.invalid"), message);
    };
    const auto* root = value.getIf<EditorValue::Object>();
    if (!root || !root->contains("schemaVersion") || !root->contains("objects") ||
        root->size() != (root->contains("schemaId") ? 3u : 2u))
        return reject("Scene snapshot requires schemaId, schemaVersion and objects");
    if (root->contains("schemaId")) {
        const auto* schema = root->at("schemaId").getIf<std::string>();
        if (!schema || *schema != "eve.scene.hierarchy") return reject("Unknown scene schema id");
    }
    const auto* version = root->at("schemaVersion").getIf<std::int64_t>();
    const auto* entries = root->at("objects").getIf<EditorValue::Array>();
    if (!version || *version != 1 || !entries) return reject("Unsupported scene snapshot schema");
    Objects objects;
    for (const auto& entry : *entries) {
        const auto* fields = entry.getIf<EditorValue::Object>();
        if (!fields || fields->size() != 4 || !fields->contains("transform"))
            return reject("Scene object requires id, parent, name and transform");
        const auto* transform = fields->at("transform").getIf<EditorValue::Object>();
        if (!transform || transform->size() != 9) return reject("Scene transform requires all nine TRS fields");
        // JSON parsers may decode integral floating point values as integers.
        EditorValue normalized = entry;
        auto&       normalizedTransform =
            *normalized.getIf<EditorValue::Object>()->at("transform").getIf<EditorValue::Object>();
        for (const auto* key : {"x", "y", "z", "rotationX", "rotationY", "rotationZ", "scaleX", "scaleY", "scaleZ"}) {
            const auto found = transform->find(key);
            if (found == transform->end()) return reject("Unknown or missing transform field");
            double number;
            if (const auto* real = found->second.getIf<double>())
                number = *real;
            else if (const auto* integer = found->second.getIf<std::int64_t>())
                number = static_cast<double>(*integer);
            else
                return reject("Transform fields must be numbers");
            if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
                return reject("Transform must be finite and representable by the scene runtime");
            if (std::string_view(key).starts_with("scale") && static_cast<float>(number) == 0.f)
                return reject("Scene scale must be nonzero");
            normalizedTransform[key] = number;
        }
        auto parsed = parseObject(normalized);
        if (!parsed.ok()) return EditorResult<Objects>::failure(parsed.status());
        auto object = std::move(parsed.value());
        if (object.id.empty() || object.name.empty() || objects.contains(object.id))
            return reject("Object ids must be unique and names nonempty");
        objects.emplace(object.id, std::move(object));
    }
    for (const auto& [id, object] : objects) {
        std::set<ObjectId> visited{id};
        auto               parent = object.parent;
        while (!parent.empty()) {
            if (!objects.contains(parent)) return reject("Scene parent does not exist");
            if (!visited.insert(parent).second) return reject("Scene hierarchy contains a cycle");
            parent = objects.at(parent).parent;
        }
    }
    return editing::applied<Objects>(std::move(objects));
}

EditorResult<DomainOperation> SceneTargetBase::makeRestore(const EditorValue& snapshot) const {
    auto validated = parseSnapshot(snapshot);
    if (!validated.ok()) return EditorResult<DomainOperation>::failure(validated.status());
    DomainOperation operation;
    operation.type = operation.inverseType = "scene.snapshot.restore.v1";
    operation.target                       = targetId();
    operation.payload                      = snapshot;
    operation.inverse                      = snapshotValue();
    operation.hasInverse                   = true;
    for (const auto& [id, object] : objects_) operation.affectedObjects.push_back({targetId(), id.value(), 0});
    for (const auto& [id, object] : validated.value())
        if (!objects_.contains(id)) operation.affectedObjects.push_back({targetId(), id.value(), 0});
    return editing::applied<DomainOperation>(std::move(operation));
}
}  // namespace eve::scene_editing
