#include "common/ActionGuideIngest.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <set>
#include <utility>

namespace eve {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalidVoid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> conflictVoid(std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move(message), std::move(path)));
}

const Value* field(const Value& value, std::string_view name) {
    if (!value.isObject()) return nullptr;
    return value.find(std::string(name));
}

Result<void> rejectUnknown(const Value& value, const std::set<std::string, std::less<>>& known, std::string path) {
    if (!value.isObject()) return invalidVoid("expected object", std::move(path));
    for (const auto& [name, item] : *value.getIf<Value::Object>()) {
        (void)item;
        if (!known.contains(name)) return invalidVoid("unknown field", path + "." + name);
    }
    return Result<void>::success();
}

Result<std::string> text(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isString()) return invalid<std::string>("expected text field", std::move(path));
    return Result<std::string>::success(item->asString());
}

Result<std::int64_t> integer(const Value& value, std::string_view name, std::string path) {
    const auto* item = field(value, name);
    if (!item || !item->isInt64()) return invalid<std::int64_t>("expected integer field", std::move(path));
    return Result<std::int64_t>::success(item->asInt());
}

std::int64_t secondsToNs(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return 0;
    return static_cast<std::int64_t>(std::llround(seconds * 1e9));
}

}  // namespace

Result<ActionGuideCandidateMeta> decodeActionGuideCandidateMeta(const Value& value) {
    auto unknown = rejectUnknown(
        value, {"schema", "schemaVersion", "sourceGuideRevision", "shotId", "model", "seed", "promptHash"},
        "candidate");
    if (!unknown) return Result<ActionGuideCandidateMeta>::failure(unknown.status());
    auto schema = text(value, "schema", "schema");
    if (!schema) return Result<ActionGuideCandidateMeta>::failure(schema.status());
    auto version = integer(value, "schemaVersion", "schemaVersion");
    if (!version) return Result<ActionGuideCandidateMeta>::failure(version.status());
    if (schema.value() != kActionGuideCandidateMetaSchemaId)
        return invalid<ActionGuideCandidateMeta>("unexpected candidate schema", "schema");
    if (version.value() != static_cast<std::int64_t>(kActionGuideCandidateMetaSchemaVersion))
        return invalid<ActionGuideCandidateMeta>("unsupported candidate schema version", "schemaVersion");
    auto revision = integer(value, "sourceGuideRevision", "sourceGuideRevision");
    if (!revision) return Result<ActionGuideCandidateMeta>::failure(revision.status());
    if (revision.value() < 0)
        return invalid<ActionGuideCandidateMeta>("sourceGuideRevision must be >= 0", "sourceGuideRevision");
    auto model = text(value, "model", "model");
    if (!model) return Result<ActionGuideCandidateMeta>::failure(model.status());
    if (model.value().empty()) return invalid<ActionGuideCandidateMeta>("model must be non-empty", "model");
    auto seed = integer(value, "seed", "seed");
    if (!seed) return Result<ActionGuideCandidateMeta>::failure(seed.status());
    auto promptHash = text(value, "promptHash", "promptHash");
    if (!promptHash) return Result<ActionGuideCandidateMeta>::failure(promptHash.status());
    if (promptHash.value().empty())
        return invalid<ActionGuideCandidateMeta>("promptHash must be non-empty", "promptHash");

    ActionGuideCandidateMeta meta;
    meta.sourceGuideRevision = static_cast<std::uint64_t>(revision.value());
    meta.model               = std::move(model).takeValue();
    meta.seed                = seed.value();
    meta.promptHash          = std::move(promptHash).takeValue();
    if (field(value, "shotId")) {
        auto shotId = text(value, "shotId", "shotId");
        if (!shotId) return Result<ActionGuideCandidateMeta>::failure(shotId.status());
        meta.shotId = std::move(shotId).takeValue();
    }
    return Result<ActionGuideCandidateMeta>::success(std::move(meta));
}

Result<Value> encodeActionGuideCandidateMeta(const ActionGuideCandidateMeta& meta) {
    if (meta.model.empty()) return invalid<Value>("model must be non-empty", "model");
    if (meta.promptHash.empty()) return invalid<Value>("promptHash must be non-empty", "promptHash");
    Value::Object root{{"schema", std::string(kActionGuideCandidateMetaSchemaId)},
                       {"schemaVersion", static_cast<std::int64_t>(kActionGuideCandidateMetaSchemaVersion)},
                       {"sourceGuideRevision", static_cast<std::int64_t>(meta.sourceGuideRevision)},
                       {"model", meta.model},
                       {"seed", meta.seed},
                       {"promptHash", meta.promptHash}};
    if (!meta.shotId.empty()) root.emplace("shotId", meta.shotId);
    return Result<Value>::success(Value(std::move(root)));
}

Result<void> validateActionGuideCandidates(const ActionGuideIngestRequest& request) {
    if (request.pack.shots.empty()) return invalidVoid("guide pack has no shots", "pack.shots");
    if (request.frames.size() != request.pack.shots.size())
        return invalidVoid("candidate frame count must equal pack.shots size", "frames");
    const auto canvasW = static_cast<std::uint32_t>(request.pack.canvas.width);
    const auto canvasH = static_cast<std::uint32_t>(request.pack.canvas.height);
    if (!canvasW || !canvasH) return invalidVoid("pack canvas is empty", "pack.canvas");

    for (size_t i = 0; i < request.frames.size(); ++i) {
        const auto& frame = request.frames[i];
        const auto  path  = "frames[" + std::to_string(i) + "]";
        if (frame.meta.sourceGuideRevision != request.pack.revision)
            return conflictVoid("candidate sourceGuideRevision does not match pack.revision",
                                path + ".sourceGuideRevision");
        if (!frame.meta.shotId.empty() && frame.meta.shotId != request.pack.shots[i].shotId)
            return invalidVoid("candidate shotId does not match pack.shots order", path + ".shotId");
        if (frame.width != canvasW || frame.height != canvasH)
            return invalidVoid("candidate frame size must match pack.canvas", path + ".size");
        if (!frame.rgba8 || frame.rgba8->size() != size_t(frame.width) * frame.height * 4)
            return invalidVoid("candidate RGBA buffer size mismatch", path + ".rgba8");

        bool hasOpaque = false;
        bool hasAlpha  = false;
        for (size_t p = 0; p + 3 < frame.rgba8->size(); p += 4) {
            const auto a = (*frame.rgba8)[p + 3];
            if (a > 8) hasOpaque = true;
            if (a < 250) hasAlpha = true;
        }
        if (!hasOpaque) return invalidVoid("candidate frame has no opaque content", path);
        if (request.requireTransparentEdge && request.pack.canvas.transparent && !hasAlpha)
            return invalidVoid("transparent canvas requires some alpha < 1 in candidates", path + ".alpha");
    }
    return Result<void>::success();
}

Result<ActionGuideIngestDraft> buildActionGuideIngestDraft(const ActionGuideIngestRequest& request) {
    auto valid = validateActionGuideCandidates(request);
    if (!valid) return Result<ActionGuideIngestDraft>::failure(valid.status());

    ActionGuideIngestDraft draft;
    draft.sourceGuideRevision = request.pack.revision;
    draft.clipName = request.outputClipName.empty() ? request.pack.actionBinding.clipName : request.outputClipName;
    if (draft.clipName.empty()) draft.clipName = "guide.clip";
    draft.fps             = request.pack.actionBinding.fps > 0.0 ? request.pack.actionBinding.fps : 12.0;
    draft.loop            = request.pack.actionBinding.loop;
    draft.frameCount      = static_cast<std::int64_t>(request.frames.size());
    draft.sequenceFirst   = 0;
    draft.sequenceLast    = draft.frameCount - 1;
    draft.sequencePattern = "frames/frame_{n}.png";

    const double frameDt = 1.0 / draft.fps;
    double       durationSeconds =
        draft.frameCount > 0 ? (static_cast<double>(draft.frameCount - 1) * frameDt + frameDt) : frameDt;
    if (!request.pack.shots.empty()) {
        const double lastShot = request.pack.shots.back().timeSeconds;
        if (std::isfinite(lastShot) && lastShot + frameDt > durationSeconds) durationSeconds = lastShot + frameDt;
    }
    double activeStart  = 0.0;
    double recoverStart = durationSeconds;
    for (const auto& mark : request.pack.actionBinding.phaseMarks) {
        if (mark.name == "active.start") activeStart = mark.timeSeconds;
        if (mark.name == "recover.start") recoverStart = mark.timeSeconds;
    }
    if (!(activeStart >= 0.0 && recoverStart >= activeStart && durationSeconds >= recoverStart)) {
        activeStart  = 0.0;
        recoverStart = durationSeconds * 0.5;
    }
    const std::int64_t windupNs  = secondsToNs(activeStart);
    const std::int64_t activeNs  = secondsToNs(recoverStart - activeStart);
    const std::int64_t recoverNs = secondsToNs(durationSeconds - recoverStart);

    Value::Array frames;
    Value::Array attribution;
    for (size_t i = 0; i < request.frames.size(); ++i) {
        const auto file = "frames/frame_" + std::to_string(i) + ".png";
        frames.emplace_back(file);
        Value::Object attr{
            {"index", static_cast<std::int64_t>(i)},
            {"shotId", request.pack.shots[i].shotId},
            {"model", request.frames[i].meta.model},
            {"seed", request.frames[i].meta.seed},
            {"promptHash", request.frames[i].meta.promptHash},
            {"sourceGuideRevision", static_cast<std::int64_t>(request.frames[i].meta.sourceGuideRevision)},
            {"file", file}};
        attribution.emplace_back(std::move(attr));
    }

    Value::Array phaseMarks;
    for (const auto& mark : request.pack.actionBinding.phaseMarks)
        phaseMarks.emplace_back(Value::Object{{"name", mark.name}, {"timeSeconds", mark.timeSeconds}});

    draft.clipDescriptor =
        Value::object({{"schema", std::string(kActionGuideClipRecipeSchemaId)},
                       {"schemaVersion", static_cast<std::int64_t>(kActionGuideClipRecipeSchemaVersion)},
                       {"clipName", draft.clipName},
                       {"fps", draft.fps},
                       {"loop", draft.loop},
                       {"sequencePattern", draft.sequencePattern},
                       {"sequenceFirst", draft.sequenceFirst},
                       {"sequenceLast", draft.sequenceLast},
                       {"frames", Value(std::move(frames))}});

    const auto abilityId = request.pack.actionBinding.abilityId.empty() ? ("ability:" + draft.clipName)
                                                                        : request.pack.actionBinding.abilityId;
    draft.abilityDraft   = Value::object(
        {{"schema", std::string(kActionGuideAbilityDraftSchemaId)},
           {"schemaVersion", static_cast<std::int64_t>(kActionGuideAbilityDraftSchemaVersion)},
           {"sourceGuideRevision", static_cast<std::int64_t>(draft.sourceGuideRevision)},
           {"abilityId", abilityId},
           {"clipName", draft.clipName},
           {"fps", draft.fps},
           {"loop", draft.loop},
           {"sequencePattern", draft.sequencePattern},
           {"sequenceFirst", draft.sequenceFirst},
           {"sequenceLast", draft.sequenceLast},
           {"timing", Value::object({{"windupNs", windupNs}, {"activeNs", activeNs}, {"recoverNs", recoverNs}})},
           {"phaseMarks", Value(std::move(phaseMarks))},
           {"commitAbility", false}});

    draft.attribution = Value::object({{"schema", std::string("eve.action.guide-ingest-attribution")},
                                       {"schemaVersion", static_cast<std::int64_t>(1)},
                                       {"frames", Value(std::move(attribution))}});
    return Result<ActionGuideIngestDraft>::success(std::move(draft));
}

}  // namespace eve
