#include "scene/editing/SceneRelations.h"

#include <cmath>
#include <map>
#include <set>
#include "editing/EditingProperty.h"

namespace eve::scene_editing {
editing::Result<std::vector<SceneRelation>> parseSceneRelations(const editing::Value&            value,
                                                                const ISceneHierarchyEditTarget& hierarchy) {
    using namespace editing;
    using Value       = editing::Value;
    using Status      = editing::Status;
    const auto reject = [](const char* message) {
        return failed<std::vector<SceneRelation>>(Status::Rejected, RuleId("scene.relations.schema"), message);
    };
    const auto* values = value.getIf<Value::Array>();
    if (!values || values->size() > 1024) return reject("Relations require an array of at most 1024 entries");
    std::set<std::string>                                                ids;
    std::map<std::pair<std::string, std::string>, eve::SceneLocalRegion> regions;
    std::vector<SceneRelation>                                           result;
    for (const auto& entry : *values) {
        const auto* object = entry.getIf<Value::Object>();
        if (!object) return reject("Each relation must be an object");
        for (const auto& [key, item] : *object) {
            (void)item;
            if (key != "id" && key != "type" && key != "subject" && key != "reference" && key != "hard" &&
                key != "priority" && key != "weight" && key != "tolerance" && key != "normalMinDot" &&
                key != "minimum" && key != "maximum" && key != "region")
                return reject("Unknown relation field");
        }
        const auto text = [&](const char* key) -> const std::string* {
            const auto found = object->find(key);
            return found == object->end() ? nullptr : found->second.getIf<std::string>();
        };
        const auto* id        = text("id");
        const auto* type      = text("type");
        const auto* subject   = text("subject");
        const auto* reference = text("reference");
        if (!id || id->empty() || !ids.insert(*id).second || !type || !subject || subject->empty() ||
            (*type != "visible_fraction" && (!reference || reference->empty() || *subject == *reference)) ||
            (*type == "visible_fraction" && object->contains("reference")))
            return reject("Relations require unique ids and two distinct nonempty object references");
        SceneRelation relation;
        relation.id      = *id;
        relation.subject = ObjectId(*subject);
        if (reference) relation.reference = ObjectId(*reference);
        if (*type == "supported_on")
            relation.kind = SceneRelationKind::SupportedOn;
        else if (*type == "nonpenetrating")
            relation.kind = SceneRelationKind::Nonpenetrating;
        else if (*type == "visible_fraction")
            relation.kind = SceneRelationKind::VisibleFraction;
        else if (*type == "occludes")
            relation.kind = SceneRelationKind::Occludes;
        else if (*type == "projected_overlap")
            relation.kind = SceneRelationKind::ProjectedOverlap;
        else
            return failed<std::vector<SceneRelation>>(
                Status::Unsupported, RuleId("scene.relations.type"),
                "Supported types are supported_on, nonpenetrating, visible_fraction, occludes and projected_overlap");
        for (const auto& participant : {relation.subject, relation.reference}) {
            if (participant.empty()) continue;
            auto resolved = hierarchy.sceneObject(participant);
            if (!resolved.ok()) return Result<std::vector<SceneRelation>>::failure(resolved.status());
        }
        if (const auto found = object->find("hard"); found != object->end()) {
            const auto* hard = found->second.getIf<bool>();
            if (!hard) return reject("hard must be boolean");
            relation.hard = *hard;
        }
        if (const auto found = object->find("priority"); found != object->end()) {
            const auto* priority = found->second.getIf<std::int64_t>();
            if (!priority || *priority < 0 || *priority > 1000) return reject("priority must be integer in [0,1000]");
            relation.priority = static_cast<int>(*priority);
        }
        const auto numeric = [&](const char* key, double& destination) {
            const auto found = object->find(key);
            if (found == object->end()) return true;
            if (const auto* real = found->second.getIf<double>())
                destination = *real;
            else if (const auto* integer = found->second.getIf<std::int64_t>())
                destination = static_cast<double>(*integer);
            else
                return false;
            return std::isfinite(destination);
        };
        if (!numeric("weight", relation.weight) || relation.weight <= 0 || relation.weight > 1e6 ||
            !numeric("tolerance", relation.tolerance) || relation.tolerance < 0 || relation.tolerance > 1 ||
            !numeric("normalMinDot", relation.normalMinDot) || relation.normalMinDot < 0 || relation.normalMinDot > 1)
            return reject("Invalid relation weight, tolerance or normalMinDot");
        const bool visual = relation.kind == SceneRelationKind::VisibleFraction ||
                            relation.kind == SceneRelationKind::Occludes ||
                            relation.kind == SceneRelationKind::ProjectedOverlap;
        if (visual) {
            if (!object->contains("minimum") || !numeric("minimum", relation.minimum) ||
                !numeric("maximum", relation.maximum) || relation.minimum < 0 || relation.maximum > 1 ||
                relation.minimum > relation.maximum || object->contains("normalMinDot") ||
                object->contains("tolerance"))
                return reject("Visual relations require minimum/maximum in [0,1]; contact fields are forbidden");
        } else if (object->contains("minimum") || object->contains("maximum"))
            return reject("Fraction fields only apply to visual relations");
        if (relation.kind != SceneRelationKind::SupportedOn && object->contains("normalMinDot"))
            return reject("normalMinDot only applies to supported_on");
        if (object->contains("region")) {
            if (!visual) return reject("Regions only apply to visual relations");
            auto region = eve::SceneLocalRegion::fromValue(editing::toPresentationValue(object->at("region")));
            if (!region.ok()) return Result<std::vector<SceneRelation>>::failure(region.status());
            const auto& measured =
                relation.kind == SceneRelationKind::VisibleFraction ? relation.subject : relation.reference;
            auto [it, inserted] = regions.emplace(std::make_pair(measured.value(), region.value().id), region.value());
            if (!inserted && it->second != region.value())
                return reject("Conflicting definitions for one object region id");
            relation.region = std::move(region).takeValue();
        }
        result.push_back(std::move(relation));
    }
    return applied<std::vector<SceneRelation>>(std::move(result));
}
}  // namespace eve::scene_editing
