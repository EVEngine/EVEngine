#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneRelations.h"

#include <array>
#include <cmath>
#include <set>

namespace eve::scene_editing {
namespace {
using namespace eve::editing;
const Value* field(const Value& value, const char* key) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}
bool number(const Value& value, double& out) {
    if (const auto* v = value.getIf<double>())
        out = *v;
    else if (const auto* v = value.getIf<std::int64_t>())
        out = static_cast<double>(*v);
    else
        return false;
    return std::isfinite(out);
}
bool vector(const Value* value, int count) {
    const auto* a = value ? value->getIf<Value::Array>() : nullptr;
    if (!a || a->size() != static_cast<std::size_t>(count)) return false;
    for (const auto& v : *a) {
        double out;
        if (!number(v, out)) return false;
    }
    return true;
}
Result<void> validate(const std::string& section, const Value& value) {
    const auto reject = [](const char* text) {
        return failed<void>(Status::Rejected, RuleId("scene.authoring.schema"), text);
    };
    const auto* object = value.getIf<Value::Object>();
    if (!object) return reject("Authoring payload must be an object");
    if (object->empty() && section != "sources") return applied<void>();
    std::set<std::string> keys;
    if (section == "sources")
        keys = {"kind",      "seed",     "mesh_params", "path",        "revision",      "tint",
                "roughness", "metallic", "visible",     "cast_shadow", "receive_shadow"};
    else if (section == "camera")
        keys = {"eye", "target", "up", "fov", "near", "far", "exposure", "ambient"};
    else if (section == "lighting")
        keys = {"background", "direction", "color"};
    else
        return reject("Unknown authoring payload section");
    for (const auto& [key, entry] : *object) {
        if (!keys.contains(key)) return reject("Unknown authoring payload field");
        if (key == "kind" || key == "path" || key == "revision") {
            const auto* text = entry.getIf<std::string>();
            if (!text || text->empty()) return reject("Source identifiers must be nonempty strings");
        } else if (key == "seed") {
            if (!entry.getIf<std::int64_t>()) return reject("Seed must be an integer");
        } else if (key == "mesh_params") {
            const auto* params = entry.getIf<Value::Object>();
            if (!params) return reject("Mesh parameters must be an object");
            for (const auto& [name, parameter] : *params) {
                (void)name;
                double numeric;
                if (!parameter.getIf<std::string>() && !number(parameter, numeric))
                    return reject("Recipe parameters must be finite numbers or strings");
            }
        } else if (key == "visible" || key == "cast_shadow" || key == "receive_shadow") {
            if (!entry.getIf<bool>()) return reject("Presentation flags must be booleans");
        } else if (key == "eye" || key == "target" || key == "up" || key == "ambient" || key == "tint" ||
                   key == "direction" || key == "background" || key == "color") {
            if (!vector(&entry, 3)) return reject("Vectors require three finite numbers");
        } else {
            double numeric;
            if (!number(entry, numeric)) return reject("Scalar must be finite");
            if ((key == "roughness" || key == "metallic") && (numeric < 0 || numeric > 1))
                return reject("Material scalars must be in [0,1]");
            if (key == "fov" && (numeric <= 0 || numeric >= 179)) return reject("Vertical FOV must be in (0,179)");
            if ((key == "near" || key == "far") && numeric <= 0) return reject("Clip distances must be positive");
        }
    }
    if (section == "sources") {
        const auto* kind = field(value, "kind");
        if (!kind) return reject("Source kind is required");
        if (*kind->getIf<std::string>() == "asset" && (!field(value, "path") || !field(value, "revision")))
            return reject("Imported sources require a path and immutable revision");
    } else if (section == "camera") {
        for (const auto* key : {"eye", "target", "up", "fov", "near", "far", "exposure", "ambient"})
            if (!field(value, key)) return reject("Camera payload must contain all camera fields");
        double nearClip, farClip;
        if (!number(*field(value, "near"), nearClip) || !number(*field(value, "far"), farClip) || nearClip >= farClip)
            return reject("Far clip must exceed near clip");
        const auto components = [&](const char* key) {
            const auto&           a = *field(value, key)->getIf<Value::Array>();
            std::array<double, 3> out{};
            for (std::size_t i = 0; i < 3; ++i) number(a[i], out[i]);
            return out;
        };
        const auto   eye = components("eye"), target = components("target"), up = components("up");
        const double x = target[0] - eye[0], y = target[1] - eye[1], z = target[2] - eye[2];
        const double cx = y * up[2] - z * up[1], cy = z * up[0] - x * up[2], cz = x * up[1] - y * up[0];
        if (cx * cx + cy * cy + cz * cz < 1e-12) return reject("Camera basis is degenerate");
    } else {
        for (const auto* key : {"background", "direction", "color"})
            if (!field(value, key)) return reject("Lighting requires background, direction and color");
        double lengthSquared = 0;
        for (const auto& component : *field(value, "direction")->getIf<Value::Array>()) {
            double numeric;
            number(component, numeric);
            lengthSquared += numeric * numeric;
        }
        if (lengthSquared < 1e-12) return reject("Light direction is degenerate");
    }
    return applied<void>();
}
}  // namespace
SceneAuthoringDocumentTarget::SceneAuthoringDocumentTarget(std::string id)
    : SceneTargetBase(std::move(id), "scene-authoring") {}
EditorValue SceneAuthoringDocumentTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaId", "eve.scene.authoring"},
                               {"schemaVersion", 4},
                               {"relations", relations_},
                               {"hierarchy", SceneTargetBase::snapshotValue()},
                               {"sources", sources_},
                               {"camera", camera_},
                               {"lighting", lighting_}};
}
Result<DomainOperation> SceneAuthoringDocumentTarget::makeRestore(const EditorValue& snapshot) const {
    const auto reject = [](const char* text) {
        return editing::failed<DomainOperation>(editing::Status::Rejected, editing::RuleId("scene.authoring.restore"),
                                                text);
    };
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return reject("Authoring snapshot must be an object");
    for (const auto* key : {"schemaId", "schemaVersion", "hierarchy", "sources", "camera", "lighting"})
        if (!root->contains(key)) return reject("Missing authoring field");
    const auto* schema  = root->at("schemaId").getIf<std::string>();
    const auto* version = root->at("schemaVersion").getIf<std::int64_t>();
    const auto* sources = root->at("sources").getIf<EditorValue::Object>();
    if (!schema || *schema != "eve.scene.authoring" || !version || (*version != 1 && *version != 2 && *version != 3 && *version != 4) ||
        !sources || root->size() != (*version == 1 ? 6u : 7u) || (*version >= 2 && !root->contains("relations")))
        return reject("Unsupported authoring schema");
    SceneDocumentTarget hierarchy("validate");
    auto                restored = hierarchy.makeRestore(root->at("hierarchy"));
    if (!restored.ok()) return Result<DomainOperation>::failure(restored.status());
    auto applied = hierarchy.applyDomainOperation(restored.value());
    if (!applied.ok()) return Result<DomainOperation>::failure(applied.status());
    for (const auto& [id, source] : *sources) {
        if (id.empty()) return reject("Source id must be nonempty");
        auto valid = validate("sources", source);
        if (!valid.ok()) return Result<DomainOperation>::failure(valid.status());
    }
    for (const auto* key : {"camera", "lighting"}) {
        auto valid = validate(key, root->at(key));
        if (!valid.ok()) return Result<DomainOperation>::failure(valid.status());
    }
    auto  canonical = snapshot;
    auto& fields    = *canonical.getIf<EditorValue::Object>();
    if (*version == 1) {
        fields["schemaVersion"] = 4;
        fields["relations"]     = EditorValue::Array{};
    }
    fields["schemaVersion"] = 4;
    if (*version < 3 && fields.at("relations").getIf<EditorValue::Array>())
        for (const auto& relation : *fields.at("relations").getIf<EditorValue::Array>()) {
            const auto* item = relation.getIf<EditorValue::Object>();
            if (item && item->contains("region")) return reject("Region relations require authoring schema v3");
        }
    if (*version < 4 && fields.at("relations").getIf<EditorValue::Array>())
        for (const auto& relation : *fields.at("relations").getIf<EditorValue::Array>()) {
            const auto* item = relation.getIf<EditorValue::Object>();
            if (item && item->contains("type") && item->at("type") == EditorValue("projected_overlap"))
                return reject("Projected overlap requires authoring schema v4");
        }
    auto relations = parseSceneRelations(fields.at("relations"), hierarchy);
    if (!relations.ok()) return Result<DomainOperation>::failure(relations.status());
    DomainOperation operation;
    operation.type = operation.inverseType = "scene.authoring.restore.v1";
    operation.target                       = targetId();
    operation.payload                      = std::move(canonical);
    operation.inverse                      = snapshotValue();
    operation.hasInverse                   = true;
    return editing::applied<DomainOperation>(std::move(operation));
}
Result<DomainOperation> SceneAuthoringDocumentTarget::makePayload(const std::string& section, const std::string& key,
                                                                  const EditorValue& payload) const {
    if (section != "relations") {
        auto valid = validate(section, payload);
        if (!valid.ok()) return Result<DomainOperation>::failure(valid.status());
    }
    if (section == "sources" && key.empty())
        return editing::failed<DomainOperation>(editing::Status::Rejected, editing::RuleId("scene.authoring.source-id"),
                                                "Source requires object identity");
    auto  candidate = snapshotValue();
    auto& values    = *candidate.getIf<EditorValue::Object>();
    if (section == "sources")
        (*values.at(section).getIf<EditorValue::Object>())[key] = payload;
    else
        values[section] = payload;
    return makeRestore(candidate);
}
Result<void> SceneAuthoringDocumentTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.type != "scene.authoring.restore.v1") return SceneTargetBase::applyDomainOperation(operation);
    if (operation.target != targetId())
        return editing::failed<void>(editing::Status::Rejected, editing::RuleId("scene.authoring.target"),
                                     "Authoring target mismatch");
    auto valid = makeRestore(operation.payload);
    if (!valid.ok()) return Result<void>::failure(valid.status());
    const auto& values = *valid.value().payload.getIf<EditorValue::Object>();
    // Prepare all owning copies before changing the hierarchy.
    auto                relations = *values.at("relations").getIf<EditorValue::Array>();
    auto                sources   = *values.at("sources").getIf<EditorValue::Object>();
    auto                camera = values.at("camera"), lighting = values.at("lighting");
    SceneDocumentTarget hierarchy("candidate");
    auto                restore = hierarchy.makeRestore(values.at("hierarchy"));
    if (!restore.ok()) return Result<void>::failure(restore.status());
    auto loaded = hierarchy.applyDomainOperation(restore.value());
    if (!loaded.ok()) return loaded;
    auto currentRestore = SceneTargetBase::makeRestore(values.at("hierarchy"));
    if (!currentRestore.ok()) return Result<void>::failure(currentRestore.status());
    // Base planning uses virtual snapshotValue for the inverse; this operation is only applied,
    // never retained. The complete authoring operation above is the history entry.
    auto changed = SceneTargetBase::applyDomainOperation(currentRestore.value());
    if (!changed.ok()) return changed;
    relations_ = std::move(relations);
    sources_   = std::move(sources);
    camera_    = std::move(camera);
    lighting_  = std::move(lighting);
    return editing::applied<void>();
}
std::unique_ptr<IDomainOperationTarget> SceneAuthoringDocumentTarget::cloneDomainState() const {
    return std::make_unique<SceneAuthoringDocumentTarget>(*this);
}
Result<void> SceneAuthoringDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* document = dynamic_cast<SceneAuthoringDocumentTarget*>(candidate.get());
    if (!document || document->targetId() != targetId())
        return editing::failed<void>(editing::Status::Rejected, editing::RuleId("scene.authoring.candidate"),
                                     "Invalid authoring candidate");
    auto relations = std::move(document->relations_);
    auto sources   = std::move(document->sources_);
    auto camera = std::move(document->camera_), lighting = std::move(document->lighting_);
    auto committed = SceneTargetBase::commitDomainState(std::move(candidate));
    if (!committed.ok()) return committed;
    relations_ = std::move(relations);
    sources_   = std::move(sources);
    camera_    = std::move(camera);
    lighting_  = std::move(lighting);
    return editing::applied<void>();
}
Result<DomainOperation> SceneAuthoringDocumentTarget::makeDelete(const editing::ObjectId& id) const {
    auto deletion = SceneTargetBase::makeDelete(id);
    if (!deletion.ok()) return deletion;
    SceneDocumentTarget hierarchy("delete-candidate");
    auto                restore = hierarchy.makeRestore(SceneTargetBase::snapshotValue());
    if (!restore.ok()) return Result<DomainOperation>::failure(restore.status());
    auto loaded = hierarchy.applyDomainOperation(restore.value());
    if (!loaded.ok()) return Result<DomainOperation>::failure(loaded.status());
    auto remove = hierarchy.makeDelete(id);
    if (!remove.ok()) return Result<DomainOperation>::failure(remove.status());
    auto removed = hierarchy.applyDomainOperation(remove.value());
    if (!removed.ok()) return Result<DomainOperation>::failure(removed.status());
    auto  snapshot    = snapshotValue();
    auto& root        = *snapshot.getIf<EditorValue::Object>();
    root["hierarchy"] = hierarchy.snapshotValue();
    EditorValue::Array retained;
    for (const auto& relation : relations_) {
        const auto& entry = *relation.getIf<EditorValue::Object>();
        if (*entry.at("subject").getIf<std::string>() != id.value() &&
            (!entry.contains("reference") || *entry.at("reference").getIf<std::string>() != id.value()))
            retained.push_back(relation);
    }
    root["relations"] = std::move(retained);
    return makeRestore(snapshot);
}
Result<void> registerAuthoringDocumentCommands(editing::IEditingCommandRegistry& registry) {
    for (const std::string section : {"sources", "camera", "lighting", "relations"}) {
        editing::EditingCommandDescriptor descriptor;
        descriptor.id                = editing::CommandId("scene.authoring." + section + ".set.v1");
        descriptor.ownerModule       = "scene_editing";
        descriptor.displayName       = "Author scene " + section;
        descriptor.category          = "Scene";
        descriptor.automationAllowed = true;
        auto registered              = registry.registerPlannedCommand(
            std::move(descriptor), [section](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto* target = dynamic_cast<SceneAuthoringDocumentTarget*>(&value);
                if (!target)
                    return editing::failed<editing::CommandPlan>(editing::Status::Unsupported,
                                                                              editing::RuleId("scene.authoring.document-required"),
                                                                              "An authoring document target is required");
                const auto* keyValue = field(request.payload, "object");
                const auto* key      = keyValue ? keyValue->getIf<std::string>() : nullptr;
                const auto* payload  = field(request.payload, "value");
                const auto* fields   = request.payload.getIf<EditorValue::Object>();
                if (!fields || fields->size() != (section == "sources" ? 2u : 1u) || !payload ||
                    (section == "sources" && (!key || key->empty())))
                    return editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                              editing::RuleId("scene.authoring.command-payload"),
                                                                              "Expected value and source object identity");
                auto operation = target->makePayload(section, key ? *key : "", *payload);
                if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
                editing::CommandPlan plan;
                plan.operations.push_back(std::move(operation.value()));
                return editing::applied<editing::CommandPlan>(std::move(plan));
            });
        if (!registered.ok()) return registered;
    }
    editing::EditingCommandDescriptor descriptor;
    descriptor.id                = editing::CommandId("scene.authoring.spawn.v1");
    descriptor.ownerModule       = "scene_editing";
    descriptor.displayName       = "Create authored asset instance";
    descriptor.category          = "Scene";
    descriptor.automationAllowed = true;
    return registry.registerPlannedCommand(
        std::move(descriptor),
        [](editing::IEditableTarget& value, const editing::CommandRequest& request) -> Result<editing::CommandPlan> {
            auto*       target  = dynamic_cast<SceneAuthoringDocumentTarget*>(&value);
            const auto* idValue = field(request.payload, "object");
            const auto* id      = idValue ? idValue->getIf<std::string>() : nullptr;
            const auto* source  = field(request.payload, "source");
            const auto* fields  = request.payload.getIf<EditorValue::Object>();
            if (!target || !id || !source || !fields)
                return editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                             editing::RuleId("scene.authoring.spawn"),
                                                             "Authoring target, object and source are required");
            for (const auto& [key, entry] : *fields) {
                (void)entry;
                if (key != "object" && key != "source" && key != "position" && key != "rotation" && key != "scale" &&
                    key != "parent")
                    return editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                 editing::RuleId("scene.authoring.spawn-field"),
                                                                 "Unknown spawn field");
            }
            CreateSceneObjectRequest create;
            create.id   = ObjectId(*id);
            create.name = *id;
            if (const auto* parent = field(request.payload, "parent")) {
                const auto* text = parent->getIf<std::string>();
                if (!text)
                    return editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                                 editing::RuleId("scene.authoring.spawn-parent"),
                                                                 "Parent must be an object id");
                create.parent = ObjectId(*text);
            }
            const auto assign = [&](const char* key, double& x, double& y, double& z) {
                const auto* entry = field(request.payload, key);
                if (!entry) return true;
                if (!vector(entry, 3)) return false;
                const auto& values = *entry->getIf<Value::Array>();
                return number(values[0], x) && number(values[1], y) && number(values[2], z);
            };
            auto& t = create.transform;
            if (!assign("position", t.x, t.y, t.z) || !assign("rotation", t.rotationX, t.rotationY, t.rotationZ) ||
                !assign("scale", t.scaleX, t.scaleY, t.scaleZ))
                return editing::failed<editing::CommandPlan>(editing::Status::Rejected,
                                                             editing::RuleId("scene.authoring.spawn-vector"),
                                                             "TRS requires finite triples");
            auto operation = target->makeCreate(create);
            if (!operation.ok()) return Result<editing::CommandPlan>::failure(operation.status());
            auto candidate = *target;
            auto created   = candidate.applyDomainOperation(operation.value());
            if (!created.ok()) return Result<editing::CommandPlan>::failure(created.status());
            auto defined = candidate.makePayload("sources", *id, *source);
            if (!defined.ok()) return Result<editing::CommandPlan>::failure(defined.status());
            auto applied = candidate.applyDomainOperation(defined.value());
            if (!applied.ok()) return Result<editing::CommandPlan>::failure(applied.status());
            auto restore = target->makeRestore(candidate.snapshotValue());
            if (!restore.ok()) return Result<editing::CommandPlan>::failure(restore.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(restore.value()));
            return editing::applied<editing::CommandPlan>(std::move(plan));
        });
}
}  // namespace eve::scene_editing
