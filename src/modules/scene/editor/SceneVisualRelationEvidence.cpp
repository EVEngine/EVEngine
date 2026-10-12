#include "scene/editor/SceneVisualRelationEvidence.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace eve::scene_editor {
editing::Result<editing::Value> evaluateSceneVisualRelations(const std::vector<scene_editing::SceneRelation>& relations,
                                                             const PhysicsPlacementFrame&                     frame,
                                                             const editing::Value& evidence, editing::Value report) {
    using Value = editing::Value;
    std::set<editing::ObjectId> selected;
    for (const auto& object : frame.objects)
        if (object.selected) selected.insert(object.object);
    const auto failure = [] {
        return editing::failed<Value>(editing::Status::Failed, editing::RuleId("scene.relations.raster-evidence"),
                                      "Malformed native visual evidence/report");
    };
    const auto* reportRoot = report.getIf<Value::Object>();
    if (!reportRoot || !reportRoot->contains("relations") || !reportRoot->at("relations").getIf<Value::Array>() ||
        !reportRoot->contains("hardSatisfied") || !reportRoot->at("hardSatisfied").getIf<bool>())
        return failure();
    auto&       root          = *report.getIf<Value::Object>();
    auto&       reports       = *root.at("relations").getIf<Value::Array>();
    bool        hardSatisfied = *root.at("hardSatisfied").getIf<bool>();
    const auto* evidenceRoot  = evidence.getIf<Value::Object>();
    if (!evidenceRoot || !evidenceRoot->contains("targets") || !evidenceRoot->at("targets").getIf<Value::Array>())
        return editing::failed<Value>(editing::Status::Failed, editing::RuleId("scene.relations.raster-evidence"),
                                      "Native raster evidence has no targets");
    std::set<std::pair<std::string, std::string>> captured;
    for (const auto& item : *evidenceRoot->at("targets").getIf<Value::Array>()) {
        const auto* target = item.getIf<Value::Object>();
        if (!target || !target->contains("object") || !target->at("object").getIf<std::string>() ||
            !target->contains("status") || !target->at("status").getIf<std::string>() ||
            !target->contains("visibleFraction") || !target->at("visibleFraction").getIf<double>() ||
            !std::isfinite(*target->at("visibleFraction").getIf<double>()) ||
            *target->at("visibleFraction").getIf<double>() < 0 || *target->at("visibleFraction").getIf<double>() > 1 ||
            !target->contains("occluders") || !target->at("occluders").getIf<Value::Array>())
            return failure();
        std::string regionId;
        if (target->contains("region")) {
            auto parsed = eve::SceneLocalRegion::fromValue(editing::toPresentationValue(target->at("region")));
            if (!parsed.ok()) return failure();
            regionId = parsed.value().id;
        }
        if (!captured.emplace(*target->at("object").getIf<std::string>(), regionId).second) return failure();
        std::set<std::string> blockers;
        for (const auto& value : *target->at("occluders").getIf<Value::Array>()) {
            const auto* blocker = value.getIf<Value::Object>();
            if (!blocker || !blocker->contains("object") || !blocker->at("object").getIf<std::string>() ||
                !blockers.insert(*blocker->at("object").getIf<std::string>()).second ||
                !blocker->contains("fraction") || !blocker->at("fraction").getIf<double>() ||
                !std::isfinite(*blocker->at("fraction").getIf<double>()) ||
                *blocker->at("fraction").getIf<double>() < 0 || *blocker->at("fraction").getIf<double>() > 1)
                return failure();
        }
    }
    for (const auto& relation : relations) {
        const bool overlap  = relation.kind == scene_editing::SceneRelationKind::ProjectedOverlap;
        const bool occludes = relation.kind == scene_editing::SceneRelationKind::Occludes;
        if (!overlap && !occludes && relation.kind != scene_editing::SceneRelationKind::VisibleFraction) continue;
        const bool           affected       = !selected.empty();
        const Value::Object* target         = nullptr;
        const auto&          measuredObject = (occludes || overlap) ? relation.reference : relation.subject;
        for (const auto& item : *evidenceRoot->at("targets").getIf<Value::Array>()) {
            const auto* object = item.getIf<Value::Object>();
            if (object && object->contains("object") &&
                *object->at("object").getIf<std::string>() == measuredObject.value()) {
                if (relation.region) {
                    if (!object->contains("region") ||
                        object->at("region") != editing::toEditingValue(relation.region->toValue()))
                        continue;
                } else if (object->contains("region"))
                    continue;
                target = object;
            }
        }
        bool   measurable = target && *target->at("status").getIf<std::string>() == "Measured";
        double fraction   = 0;
        if (overlap) {
            measurable = false;
            if (target && target->contains("projectedOverlaps")) {
                const auto* pairs = target->at("projectedOverlaps").getIf<Value::Array>();
                if (!pairs) return failure();
                std::set<std::string> partners;
                for (const auto& value : *pairs) {
                    const auto* pair = value.getIf<Value::Object>();
                    if (!pair || !pair->contains("object") || !pair->at("object").getIf<std::string>() ||
                        !partners.insert(*pair->at("object").getIf<std::string>()).second ||
                        !pair->contains("status") || !pair->at("status").getIf<std::string>() ||
                        !pair->contains("fraction") || !pair->at("fraction").getIf<double>() ||
                        !std::isfinite(*pair->at("fraction").getIf<double>()) ||
                        *pair->at("fraction").getIf<double>() < 0 || *pair->at("fraction").getIf<double>() > 1)
                        return failure();
                    for (const auto* key : {"intersectionPixels", "referencePixels", "subjectPixels"})
                        if (!pair->contains(key) || !pair->at(key).getIf<std::int64_t>() ||
                            *pair->at(key).getIf<std::int64_t>() < 0 || *pair->at(key).getIf<std::int64_t>() > 4194304)
                            return failure();
                    const auto intersection = *pair->at("intersectionPixels").getIf<std::int64_t>();
                    const auto reference    = *pair->at("referencePixels").getIf<std::int64_t>();
                    const auto subject      = *pair->at("subjectPixels").getIf<std::int64_t>();
                    if (intersection > std::min(reference, subject) ||
                        std::abs(*pair->at("fraction").getIf<double>() -
                                 (reference ? double(intersection) / reference : 0.0)) > 1e-12 ||
                        (*pair->at("status").getIf<std::string>() == "Measured" && (reference < 4 || subject < 4)))
                        return failure();
                    if (*pair->at("object").getIf<std::string>() == relation.subject.value()) {
                        measurable = *pair->at("status").getIf<std::string>() == "Measured";
                        fraction   = *pair->at("fraction").getIf<double>();
                    }
                }
            }
        } else if (measurable) {
            if (!occludes)
                fraction = *target->at("visibleFraction").getIf<double>();
            else
                for (const auto& item : *target->at("occluders").getIf<Value::Array>()) {
                    const auto& object = *item.getIf<Value::Object>();
                    if (*object.at("object").getIf<std::string>() == relation.subject.value())
                        fraction = *object.at("fraction").getIf<double>();
                }
        }
        const double residual  = std::max({relation.minimum - fraction, fraction - relation.maximum, 0.0});
        const bool   satisfied = measurable && residual == 0;
        if (affected && relation.hard && !satisfied) hardSatisfied = false;
        Value::Object item{{"id", relation.id},
                           {"type", overlap    ? "projected_overlap"
                                    : occludes ? "occludes"
                                               : "visible_fraction"},
                           {"subject", relation.subject.value()},
                           {"hard", relation.hard},
                           {"priority", relation.priority},
                           {"weight", relation.weight},
                           {"minimum", relation.minimum},
                           {"maximum", relation.maximum},
                           {"status", !affected     ? "NotEvaluated"
                                      : !measurable ? "Unmeasurable"
                                      : satisfied   ? "Satisfied"
                                                    : "Violated"},
                           {"precision", "opaque-triangle-raster"},
                           {"units", "screen-fraction"}};
        if (relation.region) item["region"] = editing::toEditingValue(relation.region->toValue());
        if (occludes || overlap) item["reference"] = relation.reference.value();
        if (affected && measurable) {
            item["fraction"] = fraction;
            item["residual"] = residual;
        }
        if (affected && !measurable)
            item["reason"] = overlap  ? Value("OverlapPairUnmeasurableOrNotCaptured")
                             : target ? target->at("status")
                                      : Value("TargetNotCaptured");
        reports.emplace_back(std::move(item));
    }
    root["hardSatisfied"]      = hardSatisfied;
    root["visibilityEvidence"] = evidence;
    root["scope"]              = "physical-incident+all-visual";
    root["precision"]          = "collision-proxy+opaque-triangle-raster";
    return editing::applied<Value>(std::move(report));
}
}  // namespace eve::scene_editor
