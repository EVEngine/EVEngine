#include "editor/EditorCurveDocument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> curveError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue keyValue(const EditorCurveKey& key) {
    return EditorValue::Object{{"id", key.id.value()}, {"time", key.time}, {"value", key.value},
        {"inTangent", key.inTangent}, {"outTangent", key.outTangent},
        {"interpolation", key.interpolation}};
}

EditorResult<EditorCurveKey> parseKey(const EditorValue& value) {
    const auto string = [&](const char* name) { const auto* entry = field(value, name); return entry ? entry->getIf<std::string>() : nullptr; };
    const auto number = [&](const char* name) { const auto* entry = field(value, name); return entry ? entry->getIf<double>() : nullptr; };
    const auto* id = string("id"); const auto* time = number("time"); const auto* assigned = number("value");
    const auto* in = number("inTangent"); const auto* out = number("outTangent"); const auto* interpolation = string("interpolation");
    const std::set<std::string> modes{"constant", "linear", "cubic"};
    if (!id || id->empty() || !time || !assigned || !in || !out || !interpolation ||
        !modes.contains(*interpolation) || !std::isfinite(*time) || !std::isfinite(*assigned) ||
        !std::isfinite(*in) || !std::isfinite(*out) || *time < 0.0 || *time > 1.0)
        return curveError<EditorCurveKey>(EditorStatus::Rejected, "editor.curve.invalid-key",
                                         "Curve key requires stable id, normalized time and finite values");
    return EditorResult<EditorCurveKey>::applied({StableId(*id), *time, *assigned, *in, *out, *interpolation});
}

EditorValue stopValue(const EditorGradientStop& stop) {
    EditorValue::Array color; for (double component : stop.color) color.emplace_back(component);
    return EditorValue::Object{{"id", stop.id.value()}, {"time", stop.time}, {"color", std::move(color)}};
}

EditorResult<EditorGradientStop> parseStop(const EditorValue& value) {
    const auto* idValue = field(value, "id"); const auto* timeValue = field(value, "time"); const auto* colorValue = field(value, "color");
    const auto* id = idValue ? idValue->getIf<std::string>() : nullptr; const auto* time = timeValue ? timeValue->getIf<double>() : nullptr;
    const auto* color = colorValue ? colorValue->getIf<EditorValue::Array>() : nullptr;
    if (!id || id->empty() || !time || !std::isfinite(*time) || *time < 0.0 || *time > 1.0 || !color || color->size() != 4)
        return curveError<EditorGradientStop>(EditorStatus::Rejected, "editor.curve.invalid-stop",
                                              "Gradient stop requires stable id, normalized time and RGBA");
    EditorGradientStop result; result.id = StableId(*id); result.time = *time;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto* number = (*color)[i].getIf<double>();
        if (!number || !std::isfinite(*number) || *number < 0.0 || *number > 1.0)
            return curveError<EditorGradientStop>(EditorStatus::Rejected,
                                                  "editor.curve.invalid-color",
                                                  "Gradient color must be normalized");
        result.color[i] = *number;
    }
    return EditorResult<EditorGradientStop>::applied(std::move(result));
}

DomainOperation operation(const char* type, const char* inverseType, const std::string& target,
                          EditorValue payload, EditorValue inverse, const StableId& affected) {
    DomainOperation result; result.type = type; result.inverseType = inverseType; result.target = TargetId(target);
    result.payload = std::move(payload); result.inverse = std::move(inverse); result.hasInverse = true;
    result.affectedObjects.push_back({TargetId(target), affected.value(), 0}); return result;
}

template <class T>
std::vector<T> timeline(const std::map<StableId, T>& values) {
    std::vector<T> result; for (const auto& [id, value] : values) { static_cast<void>(id); result.push_back(value); }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.time == b.time ? a.id < b.id : a.time < b.time; });
    return result;
}

double mix(double a, double b, double t) { return a + (b - a) * t; }

}  // namespace

EditorCurveDocument::EditorCurveDocument(std::string id) : id_(std::move(id)) {}

TargetDescriptor EditorCurveDocument::describe() const {
    return {TargetId(id_), "curve-document", revision_, false, {ICurveDocumentEditTarget::editorCapabilityId()}};
}

void* EditorCurveDocument::queryCapability(const CapabilityId& capability) {
    return capability == ICurveDocumentEditTarget::editorCapabilityId()
               ? static_cast<ICurveDocumentEditTarget*>(this) : nullptr;
}

EditorResult<void> EditorCurveDocument::applyDomainOperation(const DomainOperation& operationValue) {
    if (operationValue.target != TargetId(id_))
        return curveError<void>(EditorStatus::Rejected, "editor.curve.target-mismatch", "Curve operation targets another document");
    if (operationValue.type == "curve.key.set.v1") {
        auto parsed = parseKey(operationValue.payload); if (!parsed.value) return curveError<void>(parsed.status, "editor.curve.invalid-key", "Curve key is invalid");
        keys_[parsed.value->id] = std::move(*parsed.value);
    } else if (operationValue.type == "curve.key.delete.v1") {
        auto parsed = parseKey(operationValue.payload); if (!parsed.value || !keys_.erase(parsed.value->id)) return curveError<void>(EditorStatus::NotFound, "editor.curve.key-not-found", "Curve key was not found");
    } else if (operationValue.type == "curve.stop.set.v1") {
        auto parsed = parseStop(operationValue.payload); if (!parsed.value) return curveError<void>(parsed.status, "editor.curve.invalid-stop", "Gradient stop is invalid");
        stops_[parsed.value->id] = std::move(*parsed.value);
    } else if (operationValue.type == "curve.stop.delete.v1") {
        auto parsed = parseStop(operationValue.payload); if (!parsed.value || !stops_.erase(parsed.value->id)) return curveError<void>(EditorStatus::NotFound, "editor.curve.stop-not-found", "Gradient stop was not found");
    } else return curveError<void>(EditorStatus::Unsupported, "editor.curve.operation-unsupported", "Curve operation is unsupported");
    ++revision_; dirty_.include(0, 0); return EditorResult<void>::applied();
}

std::unique_ptr<IDomainOperationTarget> EditorCurveDocument::cloneDomainState() const {
    return std::make_unique<EditorCurveDocument>(*this);
}

EditorResult<void> EditorCurveDocument::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* curve = dynamic_cast<EditorCurveDocument*>(candidate.get());
    if (!curve || curve->id_ != id_) return curveError<void>(EditorStatus::Rejected, "editor.curve.invalid-candidate", "Curve candidate does not match this document");
    *this = std::move(*curve); return EditorResult<void>::applied();
}

EditorResult<DomainOperation> EditorCurveDocument::makeSetKey(const EditorCurveKey& key) const {
    auto parsed = parseKey(keyValue(key)); if (!parsed.value) return curveError<DomainOperation>(parsed.status, "editor.curve.invalid-key", "Curve key is invalid");
    const auto found = keys_.find(key.id); const bool exists = found != keys_.end();
    return EditorResult<DomainOperation>::applied(operation("curve.key.set.v1", exists ? "curve.key.set.v1" : "curve.key.delete.v1", id_, keyValue(*parsed.value), exists ? keyValue(found->second) : keyValue(*parsed.value), key.id));
}

EditorResult<DomainOperation> EditorCurveDocument::makeDeleteKey(const StableId& key) const {
    const auto found = keys_.find(key); if (found == keys_.end()) return curveError<DomainOperation>(EditorStatus::NotFound, "editor.curve.key-not-found", "Curve key was not found");
    return EditorResult<DomainOperation>::applied(operation("curve.key.delete.v1", "curve.key.set.v1", id_, keyValue(found->second), keyValue(found->second), key));
}

EditorResult<DomainOperation> EditorCurveDocument::makeSetStop(const EditorGradientStop& stop) const {
    auto parsed = parseStop(stopValue(stop)); if (!parsed.value) return curveError<DomainOperation>(parsed.status, "editor.curve.invalid-stop", "Gradient stop is invalid");
    const auto found = stops_.find(stop.id); const bool exists = found != stops_.end();
    return EditorResult<DomainOperation>::applied(operation("curve.stop.set.v1", exists ? "curve.stop.set.v1" : "curve.stop.delete.v1", id_, stopValue(*parsed.value), exists ? stopValue(found->second) : stopValue(*parsed.value), stop.id));
}

EditorResult<DomainOperation> EditorCurveDocument::makeDeleteStop(const StableId& stop) const {
    const auto found = stops_.find(stop); if (found == stops_.end()) return curveError<DomainOperation>(EditorStatus::NotFound, "editor.curve.stop-not-found", "Gradient stop was not found");
    return EditorResult<DomainOperation>::applied(operation("curve.stop.delete.v1", "curve.stop.set.v1", id_, stopValue(found->second), stopValue(found->second), stop));
}

std::vector<EditorCurveKey> EditorCurveDocument::keys() const { return timeline(keys_); }
std::vector<EditorGradientStop> EditorCurveDocument::stops() const { return timeline(stops_); }

double EditorCurveDocument::sampleCurve(double time) const {
    const auto values = keys(); if (values.empty()) return 0.0;
    time = std::clamp(time, 0.0, 1.0);
    const auto upper = std::upper_bound(values.begin(), values.end(), time, [](double t, const auto& key) { return t < key.time; });
    if (upper == values.begin()) return upper->value;
    if (upper == values.end()) return values.back().value;
    const auto& a = *(upper - 1); const auto& b = *upper; const double span = b.time - a.time;
    const double t = span > 0.0 ? (time - a.time) / span : 0.0;
    if (a.interpolation == "constant") return a.value;
    if (a.interpolation == "linear") return mix(a.value, b.value, t);
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * a.outTangent * span +
           (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * b.inTangent * span;
}

std::array<double, 4> EditorCurveDocument::sampleGradient(double time) const {
    const auto values = stops(); if (values.empty()) return {1.0, 1.0, 1.0, 1.0};
    time = std::clamp(time, 0.0, 1.0);
    const auto upper = std::upper_bound(values.begin(), values.end(), time, [](double t, const auto& stop) { return t < stop.time; });
    if (upper == values.begin()) return upper->color;
    if (upper == values.end()) return values.back().color;
    const auto& a = *(upper - 1); const auto& b = *upper; const double t = b.time > a.time ? (time - a.time) / (b.time - a.time) : 0.0;
    std::array<double, 4> result; for (std::size_t i = 0; i < 4; ++i) result[i] = mix(a.color[i], b.color[i], t); return result;
}

EditorCurvePreview EditorCurveDocument::preview(int sampleCount, int maximumSamples) const {
    EditorCurvePreview result; result.documentRevision = revision_;
    if (sampleCount < 2 || maximumSamples < 2 || sampleCount > maximumSamples) {
        result.status = EditorStatus::Rejected; result.diagnostics.push_back({RuleId("editor.curve.preview-budget"), DiagnosticSeverity::Error, "Curve preview sample count exceeds its bounded budget"}); return result;
    }
    result.curveSamples.reserve(static_cast<std::size_t>(sampleCount)); result.gradientSamples.reserve(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) { const double t = static_cast<double>(i) / (sampleCount - 1); result.curveSamples.push_back(sampleCurve(t)); result.gradientSamples.push_back(sampleGradient(t)); }
    result.status = EditorStatus::Applied; return result;
}

EditorValue EditorCurveDocument::snapshotValue() const {
    EditorValue::Array keys; for (const auto& key : this->keys()) keys.push_back(keyValue(key));
    EditorValue::Array stops; for (const auto& stop : this->stops()) stops.push_back(stopValue(stop));
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"keys", std::move(keys)}, {"stops", std::move(stops)}};
}

EditorResult<void> EditorCurveDocument::loadSnapshot(const EditorValue& snapshot) {
    const auto* versionValue = field(snapshot, "schemaVersion"); const auto* keysValue = field(snapshot, "keys"); const auto* stopsValue = field(snapshot, "stops");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr; const auto* keys = keysValue ? keysValue->getIf<EditorValue::Array>() : nullptr; const auto* stops = stopsValue ? stopsValue->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !keys || !stops) return curveError<void>(EditorStatus::Unsupported, "editor.curve.invalid-snapshot", "Curve snapshot schema is unsupported");
    EditorCurveDocument candidate(id_);
    for (const auto& value : *keys) { auto parsed = parseKey(value); if (!parsed.value || candidate.keys_.contains(parsed.value->id)) return curveError<void>(EditorStatus::Rejected, "editor.curve.invalid-snapshot-key", "Curve snapshot contains invalid or duplicate keys"); candidate.keys_[parsed.value->id] = std::move(*parsed.value); }
    for (const auto& value : *stops) { auto parsed = parseStop(value); if (!parsed.value || candidate.stops_.contains(parsed.value->id)) return curveError<void>(EditorStatus::Rejected, "editor.curve.invalid-snapshot-stop", "Curve snapshot contains invalid or duplicate stops"); candidate.stops_[parsed.value->id] = std::move(*parsed.value); }
    candidate.revision_ = revision_ + 1; candidate.dirty_.clear(); *this = std::move(candidate); return EditorResult<void>::applied();
}

}  // namespace eve::editor
