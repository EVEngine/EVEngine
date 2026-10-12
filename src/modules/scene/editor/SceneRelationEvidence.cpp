#include "scene/editor/SceneRelationEvidence.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace eve::scene_editor {
editing::Result<editing::Value> evaluateSceneRelations(const std::vector<scene_editing::SceneRelation>& relations,
                                                       const PhysicsPlacementFrame&                     frame) {
    using namespace editing;
    using Value  = editing::Value;
    using Status = editing::Status;
    std::map<ObjectId, bool> selected;
    for (const auto& object : frame.objects) selected.emplace(object.object, object.selected);
    Value::Array reports;
    Value::Array previewObjects;
    for (const auto& object : frame.objects) {
        if (!object.selected) continue;
        const auto& t = object.transform;
        previewObjects.emplace_back(Value::Object{{"object", object.object.value()},
                                                  {"position", Value::Array{t.x, t.y, t.z}},
                                                  {"rotation", Value::Array{t.rotationX, t.rotationY, t.rotationZ}},
                                                  {"scale", Value::Array{t.scaleX, t.scaleY, t.scaleZ}}});
    }
    bool hardSatisfied = true;
    for (const auto& relation : relations) {
        if (relation.kind == scene_editing::SceneRelationKind::VisibleFraction ||
            relation.kind == scene_editing::SceneRelationKind::Occludes ||
            relation.kind == scene_editing::SceneRelationKind::ProjectedOverlap)
            continue;
        const auto subject = selected.find(relation.subject), reference = selected.find(relation.reference);
        const bool admitted = subject != selected.end() && reference != selected.end();
        const bool affected =
            (subject != selected.end() && subject->second) || (reference != selected.end() && reference->second);
        const bool measurable = admitted && frame.tick > 0 && frame.contactsComplete &&
                                (subject->second || reference->second) &&
                                (!(subject->second && reference->second) || frame.selectedPairCollisionsEnabled);
        double penetration = 0, supportDot = -1, supportGap = 0;
        int    contacts = 0;
        bool   support  = false;
        for (const auto& contact : frame.contacts) {
            if (contact.subject != relation.subject || contact.reference != relation.reference) continue;
            ++contacts;
            if (!std::isfinite(contact.separation) || !std::isfinite(contact.normalY))
                return failed<Value>(Status::Failed, RuleId("scene.relations.evidence"), "Nonfinite contact evidence");
            penetration      = std::max(penetration, -contact.separation);
            const double dot = -contact.normalY;  // world-up support, subject normal points downward
            supportDot       = std::max(supportDot, dot);
            if (dot >= relation.normalMinDot && std::abs(contact.separation) <= relation.tolerance) {
                support    = true;
                supportGap = std::max(supportGap, std::abs(contact.separation));
            }
        }
        const bool supportedOn = relation.kind == scene_editing::SceneRelationKind::SupportedOn;
        const bool satisfied   = measurable && penetration <= relation.tolerance && (!supportedOn || support);
        if (relation.hard && affected && !satisfied) hardSatisfied = false;
        Value::Object report{{"id", relation.id},
                             {"subject", relation.subject.value()},
                             {"reference", relation.reference.value()},
                             {"hard", relation.hard},
                             {"priority", relation.priority},
                             {"weight", relation.weight},
                             {"type", supportedOn ? "supported_on" : "nonpenetrating"},
                             {"status", !affected     ? "NotEvaluated"
                                        : !measurable ? "Unmeasurable"
                                        : satisfied   ? "Satisfied"
                                                      : "Violated"},
                             {"contactCount", contacts},
                             {"penetration", penetration},
                             {"tolerance", relation.tolerance},
                             {"units", "scene-length"},
                             {"precision", "collision-proxy"}};
        if (supportedOn) {
            report["supportNormalDot"]  = supportDot;
            report["requiredNormalDot"] = relation.normalMinDot;
            if (support) report["contactGap"] = supportGap;
            report["residual"]       = std::max(penetration - relation.tolerance, 0.0);
            report["missingSupport"] = !support;
        } else
            report["residual"] = std::max(penetration - relation.tolerance, 0.0);
        if (affected && !measurable)
            report["reason"] = !admitted                 ? "ParticipantNotAdmitted"
                               : frame.tick == 0         ? "NotStepped"
                               : !frame.contactsComplete ? "ContactBudgetExceeded"
                                                         : "PairNotSimulated";
        reports.emplace_back(std::move(report));
    }
    return applied<Value>(Value::Object{{"schemaId", "eve.scene.relation-report"},
                                        {"schemaVersion", 1},
                                        {"hardSatisfied", hardSatisfied},
                                        {"relations", std::move(reports)},
                                        {"previewObjects", std::move(previewObjects)},
                                        {"transformSpace", "world"},
                                        {"tick", static_cast<std::int64_t>(frame.tick)},
                                        {"precision", "collision-proxy"},
                                        {"contactsComplete", frame.contactsComplete},
                                        {"scope", "incident-to-selection"}});
}
}  // namespace eve::scene_editor
