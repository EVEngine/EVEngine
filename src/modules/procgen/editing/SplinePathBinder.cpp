#include "procgen/editing/SplinePathBinder.h"

#include "common/SceneQuery.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen_editing {
namespace {

template <class T>
EditorResult<T> binderError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue bindingValue(const SplinePointBinding& binding) {
    return EditorValue::Object{{"point", EditorValue(binding.point.value())},
                               {"host", EditorValue(binding.host)},
                               {"object", EditorValue(binding.object)}};
}

}  // namespace

EditorResult<SplineBindingPose> SceneQuerySplineBindingSource::resolve(const std::string& host,
                                                                       const std::string& object) const {
    if (!query_)
        return binderError<SplineBindingPose>(EditorStatus::Unsupported, "editor.spline.binding-provider-absent",
                                              "Scene transform provider is not available");
    SceneNodeInfo info;
    if (!query_->getNodeIn(host, object, &info))
        return binderError<SplineBindingPose>(EditorStatus::Conflict, "editor.spline.binding-source-stale",
                                              "Bound scene transform is missing or stale");
    return eve::editing::applied<SplineBindingPose>({info.x, info.y, info.z});
}

SplinePathBinder::SplinePathBinder(std::string documentTarget) : documentTarget_(std::move(documentTarget)) {}

EditorResult<void> SplinePathBinder::bindResult(const SplinePathDocument& document, const StableId& point,
                                                std::string host, std::string object,
                                                const ISplineBindingTransformSource& source) {
    if (document.targetId().value() != documentTarget_)
        return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-target-mismatch",
                                 "Spline binder targets another document");
    const auto points = document.points();
    if (point.empty() ||
        std::none_of(points.begin(), points.end(), [&](const auto& value) { return value.id == point; }))
        return binderError<void>(EditorStatus::NotFound, "editor.spline.binding-point-missing",
                                 "Bound spline point does not exist");
    if (host.empty() || object.empty())
        return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-identity-empty",
                                 "Spline binding requires non-empty host and object identities");
    auto resolved = source.resolve(host, object);
    if (!resolved.ok())
        return binderError<void>(resolved.code(), "editor.spline.binding-source-stale", resolved.status().describe());
    bindings_[point] = {point, std::move(host), std::move(object)};
    return eve::editing::applied<void>();
}

EditorResult<void> SplinePathBinder::unbindResult(const StableId& point) {
    if (!bindings_.erase(point))
        return binderError<void>(EditorStatus::NotFound, "editor.spline.binding-not-found",
                                 "Spline point binding was not found");
    return eve::editing::applied<void>();
}

EditorResult<void> SplinePathBinder::refreshResult(SplinePathDocument&                  document,
                                                   const ISplineBindingTransformSource& source) const {
    if (document.targetId().value() != documentTarget_)
        return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-target-mismatch",
                                 "Spline binder targets another document");
    auto       candidate = document;
    const auto points    = document.points();
    for (const auto& [id, binding] : bindings_) {
        const auto found =
            std::find_if(points.begin(), points.end(), [&](const auto& value) { return value.id == id; });
        if (found == points.end())
            return binderError<void>(EditorStatus::Conflict, "editor.spline.binding-point-stale",
                                     "Bound spline point was removed");
        auto pose = source.resolve(binding.host, binding.object);
        if (!pose.ok())
            return binderError<void>(pose.code(), "editor.spline.binding-source-stale", pose.status().describe());
        if (!std::isfinite(pose.value().x) || !std::isfinite(pose.value().y) || !std::isfinite(pose.value().z))
            return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-pose-invalid",
                                     "Bound transform returned non-finite coordinates");
        auto updated   = *found;
        updated.x      = pose.value().x;
        updated.y      = pose.value().y;
        updated.z      = pose.value().z;
        auto operation = candidate.makeSetPoint(updated);
        if (!operation.ok())
            return binderError<void>(operation.code(), "editor.spline.binding-operation-failed",
                                     operation.status().describe());
        auto applied = candidate.applyDomainOperation(operation.value());
        if (!applied.ok()) return applied;
    }
    return document.commitDomainState(std::make_unique<SplinePathDocument>(std::move(candidate)));
}

std::vector<SplinePointBinding> SplinePathBinder::bindings() const {
    std::vector<SplinePointBinding> result;
    result.reserve(bindings_.size());
    for (const auto& [unused, binding] : bindings_) {
        (void)unused;
        result.push_back(binding);
    }
    return result;
}

EditorValue SplinePathBinder::snapshotValue() const {
    EditorValue::Array bindings;
    for (const auto& [unused, binding] : bindings_) {
        (void)unused;
        bindings.emplace_back(bindingValue(binding));
    }
    return EditorValue::Object{{"schema", EditorValue("eve.procgen.splineBinder")},
                               {"version", EditorValue(std::int64_t{1})},
                               {"document", EditorValue(documentTarget_)},
                               {"bindings", EditorValue(std::move(bindings))}};
}

EditorResult<void> SplinePathBinder::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaValue   = field(snapshot, "schema");
    const auto* versionValue  = field(snapshot, "version");
    const auto* documentValue = field(snapshot, "document");
    const auto* bindingsValue = field(snapshot, "bindings");
    const auto* schema        = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version       = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* document      = documentValue ? documentValue->getIf<std::string>() : nullptr;
    const auto* values        = bindingsValue ? bindingsValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || *schema != "eve.procgen.splineBinder" || !version || *version != 1)
        return binderError<void>(EditorStatus::Unsupported, "editor.spline.binding-schema-unsupported",
                                 "Unsupported spline binder snapshot schema");
    if (!document || *document != documentTarget_ || !values)
        return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-snapshot-invalid",
                                 "Spline binder snapshot targets another document or has invalid bindings");
    std::map<StableId, SplinePointBinding> candidate;
    for (const auto& value : *values) {
        const auto* pointValue  = field(value, "point");
        const auto* hostValue   = field(value, "host");
        const auto* objectValue = field(value, "object");
        const auto* point       = pointValue ? pointValue->getIf<std::string>() : nullptr;
        const auto* host        = hostValue ? hostValue->getIf<std::string>() : nullptr;
        const auto* object      = objectValue ? objectValue->getIf<std::string>() : nullptr;
        if (!point || point->empty() || !host || host->empty() || !object || object->empty() ||
            !candidate.emplace(StableId(*point), SplinePointBinding{StableId(*point), *host, *object}).second)
            return binderError<void>(EditorStatus::Rejected, "editor.spline.binding-snapshot-invalid",
                                     "Spline binder snapshot contains invalid or duplicate links");
    }
    bindings_ = std::move(candidate);
    return eve::editing::applied<void>();
}

}  // namespace eve::procgen_editing
