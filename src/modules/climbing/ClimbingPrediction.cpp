#include "climbing/Climbing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.prediction"));
}

constexpr std::uint64_t fnvOffset = 14695981039346656037ull;
constexpr std::uint64_t fnvPrime = 1099511628211ull;

void appendByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnvPrime;
}

template <class T>
void appendInteger(std::uint64_t& hash, T value) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        appendByte(hash, static_cast<std::uint8_t>((bits >> (index * 8)) & static_cast<Unsigned>(0xff)));
}

void appendString(std::uint64_t& hash, std::string_view value) noexcept {
    appendInteger(hash, static_cast<std::uint64_t>(value.size()));
    for (const unsigned char character : value) appendByte(hash, character);
}

std::int64_t quantize(float value) noexcept {
    if (!std::isfinite(value)) return std::numeric_limits<std::int64_t>::min();
    const double scaled = std::round(static_cast<double>(value) * 1000.0);
    if (scaled <= static_cast<double>(std::numeric_limits<std::int64_t>::min()))
        return std::numeric_limits<std::int64_t>::min();
    if (scaled >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(scaled);
}

void appendVec(std::uint64_t& hash, Vec3 value) noexcept {
    appendInteger(hash, quantize(value.x));
    appendInteger(hash, quantize(value.y));
    appendInteger(hash, quantize(value.z));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const auto* value = field(object, name);
    const auto* text = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool parseUint64(std::string_view text, std::uint64_t& output, int base = 10) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    const auto parsed = std::from_chars(first, last, output, base);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

bool readUint64String(const eve::Value::Object& object, std::string_view name, std::uint64_t& output,
                      int base = 10) {
    std::string text;
    return readString(object, name, text) && parseUint64(text, output, base);
}

bool readInt64(const eve::Value::Object& object, std::string_view name, std::int64_t& output) {
    const auto* value = field(object, name);
    const auto* integer = value ? value->getIf<std::int64_t>() : nullptr;
    if (!integer) return false;
    output = *integer;
    return true;
}

eve::Value::Object unknownFields(const eve::Value::Object& object,
                                 std::initializer_list<std::string_view> known) {
    const std::unordered_set<std::string_view> names(known.begin(), known.end());
    eve::Value::Object result;
    for (const auto& [name, value] : object)
        if (!names.contains(name)) result.emplace(name, value);
    return result;
}

std::string_view dispositionName(ClimbingPredictionDisposition value) {
    switch (value) {
        case ClimbingPredictionDisposition::Accepted: return "accepted";
        case ClimbingPredictionDisposition::Corrected: return "corrected";
        case ClimbingPredictionDisposition::Rejected: return "rejected";
    }
    return "rejected";
}

bool readDisposition(std::string_view text, ClimbingPredictionDisposition& output) {
    if (text == "accepted") output = ClimbingPredictionDisposition::Accepted;
    else if (text == "corrected") output = ClimbingPredictionDisposition::Corrected;
    else if (text == "rejected") output = ClimbingPredictionDisposition::Rejected;
    else return false;
    return true;
}

std::string_view reasonName(ClimbingPredictionReason value) {
    switch (value) {
        case ClimbingPredictionReason::None: return "none";
        case ClimbingPredictionReason::CandidateMismatch: return "candidate_mismatch";
        case ClimbingPredictionReason::NoCandidate: return "no_candidate";
        case ClimbingPredictionReason::TickTooFarAhead: return "tick_too_far_ahead";
        case ClimbingPredictionReason::RuntimeBusy: return "runtime_busy";
        case ClimbingPredictionReason::InvalidRequest: return "invalid_request";
    }
    return "invalid_request";
}

bool readReason(std::string_view text, ClimbingPredictionReason& output) {
    if (text == "none") output = ClimbingPredictionReason::None;
    else if (text == "candidate_mismatch") output = ClimbingPredictionReason::CandidateMismatch;
    else if (text == "no_candidate") output = ClimbingPredictionReason::NoCandidate;
    else if (text == "tick_too_far_ahead") output = ClimbingPredictionReason::TickTooFarAhead;
    else if (text == "runtime_busy") output = ClimbingPredictionReason::RuntimeBusy;
    else if (text == "invalid_request") output = ClimbingPredictionReason::InvalidRequest;
    else return false;
    return true;
}

eve::Value encodeKey(const ClimbingCandidateKey& key) {
    return eve::Value::object({
        {"actionId", key.actionId},
        {"kind", static_cast<std::int64_t>(key.kind)},
        {"definitionGeneration", std::to_string(key.definitionGeneration)},
        {"sortedRank", static_cast<std::int64_t>(key.sortedRank)},
        {"fingerprintHex", [&key] {
             char buffer[17]{};
             const auto converted = std::to_chars(buffer, buffer + 16, key.fingerprint, 16);
             return std::string(buffer, converted.ptr);
         }()},
    });
}

bool decodeKey(const eve::Value& value, ClimbingCandidateKey& key) {
    const auto* object = value.getIf<eve::Value::Object>();
    std::int64_t kind = 0;
    std::int64_t rank = 0;
    if (!object || !readString(*object, "actionId", key.actionId) ||
        !readInt64(*object, "kind", kind) || kind < 0 ||
        kind > static_cast<std::int64_t>(ClimbingActionKind::BarSwing) ||
        !readUint64String(*object, "definitionGeneration", key.definitionGeneration) ||
        !readInt64(*object, "sortedRank", rank) || rank < 0 ||
        rank > std::numeric_limits<std::uint32_t>::max() ||
        !readUint64String(*object, "fingerprintHex", key.fingerprint, 16))
        return false;
    key.kind = static_cast<ClimbingActionKind>(kind);
    key.sortedRank = static_cast<std::uint32_t>(rank);
    return true;
}

bool validKey(const ClimbingCandidateKey& key) {
    return !key.actionId.empty() && key.definitionGeneration != 0 && key.fingerprint != 0;
}

eve::Result<void> validateSnapshotEnvelope(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    std::string schemaId;
    std::int64_t schemaVersion = -1;
    if (!object || !readString(*object, "schemaId", schemaId) ||
        schemaId != ClimbingRuntime::SnapshotSchemaId ||
        !readInt64(*object, "schemaVersion", schemaVersion) || schemaVersion < 0)
        return failure<void>(eve::DiagnosticCode::ParseError,
                             "authoritative snapshot has an invalid climbing runtime envelope",
                             "authoritativeSnapshot");
    if (schemaVersion > ClimbingRuntime::SnapshotSchemaVersion)
        return failure<void>(eve::DiagnosticCode::UnknownVersion,
                             "authoritative snapshot version is newer than this runtime",
                             "authoritativeSnapshot.schemaVersion");
    return eve::Result<void>::success();
}

bool consistentDecision(const ClimbingPredictionDecision& decision) {
    if (decision.sequence.isZero()) return false;
    if (decision.disposition == ClimbingPredictionDisposition::Accepted)
        return validKey(decision.authoritativeCandidate) && decision.reason == ClimbingPredictionReason::None;
    if (decision.reason == ClimbingPredictionReason::None) return false;
    return decision.disposition != ClimbingPredictionDisposition::Corrected ||
           validKey(decision.authoritativeCandidate);
}

eve::Result<eve::Value::Object> readEnvelope(const eve::Value& value, std::string_view schemaId) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object)
        return failure<eve::Value::Object>(eve::DiagnosticCode::ParseError,
                                           "prediction payload must be an object");
    std::string actualSchema;
    std::int64_t version = 0;
    if (!readString(*object, "schemaId", actualSchema) || actualSchema != schemaId)
        return failure<eve::Value::Object>(eve::DiagnosticCode::ParseError,
                                           "prediction schemaId is missing or mismatched", "schemaId");
    if (!readInt64(*object, "schemaVersion", version))
        return failure<eve::Value::Object>(eve::DiagnosticCode::ParseError,
                                           "prediction schemaVersion must be an integer", "schemaVersion");
    if (version != 1)
        return failure<eve::Value::Object>(eve::DiagnosticCode::UnknownVersion,
                                           "prediction schema version is unsupported", "schemaVersion");
    return eve::Result<eve::Value::Object>::success(*object);
}

bool activePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed &&
           phase != ClimbingPhase::Cancelled && phase != ClimbingPhase::Failed;
}

}  // namespace

ClimbingCandidateKey makeClimbingCandidateKey(const ClimbingCandidate& candidate,
                                               std::uint32_t sortedRank) noexcept {
    std::uint64_t hash = fnvOffset;
    appendString(hash, candidate.actionId);
    appendInteger(hash, static_cast<std::uint8_t>(candidate.kind));
    appendInteger(hash, candidate.definitionGeneration);
    appendInteger(hash, sortedRank);
    appendInteger(hash, candidate.obstacleBodyId);
    appendInteger(hash, candidate.obstacleShapeId);
    appendVec(hash, candidate.frontPoint);
    appendVec(hash, candidate.bodyLocalTop);
    appendVec(hash, candidate.bodyLocalLanding);
    appendVec(hash, candidate.surfaceNormal);
    appendVec(hash, candidate.surfaceTangent);
    appendInteger(hash, quantize(candidate.obstacleHeight));
    appendInteger(hash, quantize(candidate.obstacleDepth));
    appendInteger(hash, quantize(candidate.gapDistance));
    appendInteger(hash, quantize(candidate.clearanceHeight));
    appendInteger(hash, quantize(candidate.slopeRadians));
    appendInteger(hash, quantize(candidate.curvature));
    appendInteger(hash, candidate.supportShapeTag);
    appendInteger(hash, candidate.supportMaterialId);
    appendInteger(hash, static_cast<std::uint8_t>(candidate.probeRecipe));
    appendInteger(hash, candidate.score);
    appendInteger(hash, static_cast<std::uint8_t>(candidate.support));
    if (hash == 0) hash = 1;
    return {candidate.actionId, candidate.kind, candidate.definitionGeneration, sortedRank, hash};
}

eve::Result<eve::Value> encodeClimbingPredictionRequest(const ClimbingPredictionRequest& request) {
    if (request.sequence.isZero() || !validKey(request.candidate))
        return failure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                   "prediction request requires a non-zero sequence and candidate key", "request");
    eve::Value::Object object = request.extensionMetadata;
    object["schemaId"] = eve::Value(std::string(ClimbingPredictionRequest::SchemaId));
    object["schemaVersion"] = eve::Value(ClimbingPredictionRequest::SchemaVersion);
    object["sequence"] = eve::Value(std::to_string(request.sequence.value()));
    object["clientTick"] = eve::Value(std::to_string(request.clientTick.value()));
    object["candidate"] = encodeKey(request.candidate);
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<ClimbingPredictionRequest> decodeClimbingPredictionRequest(const eve::Value& value) {
    auto root = readEnvelope(value, ClimbingPredictionRequest::SchemaId);
    if (!root) return eve::Result<ClimbingPredictionRequest>::failure(root.status());
    std::uint64_t sequence = 0;
    std::uint64_t clientTick = 0;
    ClimbingPredictionRequest request;
    const eve::Value* candidate = field(root.value(), "candidate");
    if (!readUint64String(root.value(), "sequence", sequence) || sequence == 0 ||
        !readUint64String(root.value(), "clientTick", clientTick) || !candidate ||
        !decodeKey(*candidate, request.candidate) || !validKey(request.candidate))
        return failure<ClimbingPredictionRequest>(eve::DiagnosticCode::ParseError,
                                                  "prediction request has missing or invalid fields", "request");
    request.sequence = ClimbingPredictionSequence(sequence);
    request.clientTick = eve::SimulationTick(clientTick);
    request.extensionMetadata = unknownFields(root.value(),
                                               {"schemaId", "schemaVersion", "sequence", "clientTick", "candidate"});
    return eve::Result<ClimbingPredictionRequest>::success(std::move(request));
}

eve::Result<eve::Value> encodeClimbingPredictionDecision(const ClimbingPredictionDecision& decision) {
    if (!consistentDecision(decision))
        return failure<eve::Value>(eve::DiagnosticCode::InvalidArgument,
                                   "prediction decision fields are inconsistent", "decision");
    auto snapshotEnvelope = validateSnapshotEnvelope(decision.authoritativeSnapshot);
    if (!snapshotEnvelope) return eve::Result<eve::Value>::failure(snapshotEnvelope.status());
    eve::Value::Object object = decision.extensionMetadata;
    object["schemaId"] = eve::Value(std::string(ClimbingPredictionDecision::SchemaId));
    object["schemaVersion"] = eve::Value(ClimbingPredictionDecision::SchemaVersion);
    object["sequence"] = eve::Value(std::to_string(decision.sequence.value()));
    object["clientTick"] = eve::Value(std::to_string(decision.clientTick.value()));
    object["serverTick"] = eve::Value(std::to_string(decision.serverTick.value()));
    object["disposition"] = eve::Value(std::string(dispositionName(decision.disposition)));
    object["reason"] = eve::Value(std::string(reasonName(decision.reason)));
    object["authoritativeCandidate"] = encodeKey(decision.authoritativeCandidate);
    object["authoritativeSnapshot"] = decision.authoritativeSnapshot;
    return eve::Result<eve::Value>::success(eve::Value(std::move(object)));
}

eve::Result<ClimbingPredictionDecision> decodeClimbingPredictionDecision(const eve::Value& value) {
    auto root = readEnvelope(value, ClimbingPredictionDecision::SchemaId);
    if (!root) return eve::Result<ClimbingPredictionDecision>::failure(root.status());
    std::uint64_t sequence = 0;
    std::uint64_t clientTick = 0;
    std::uint64_t serverTick = 0;
    std::string disposition;
    std::string reason;
    ClimbingPredictionDecision decision;
    const eve::Value* key = field(root.value(), "authoritativeCandidate");
    const eve::Value* snapshot = field(root.value(), "authoritativeSnapshot");
    if (!readUint64String(root.value(), "sequence", sequence) || sequence == 0 ||
        !readUint64String(root.value(), "clientTick", clientTick) ||
        !readUint64String(root.value(), "serverTick", serverTick) ||
        !readString(root.value(), "disposition", disposition) ||
        !readDisposition(disposition, decision.disposition) || !readString(root.value(), "reason", reason) ||
        !readReason(reason, decision.reason) || !key || !decodeKey(*key, decision.authoritativeCandidate) ||
        !snapshot)
        return failure<ClimbingPredictionDecision>(eve::DiagnosticCode::ParseError,
                                                    "prediction decision has missing or invalid fields", "decision");
    decision.sequence = ClimbingPredictionSequence(sequence);
    decision.clientTick = eve::SimulationTick(clientTick);
    decision.serverTick = eve::SimulationTick(serverTick);
    if (!consistentDecision(decision))
        return failure<ClimbingPredictionDecision>(eve::DiagnosticCode::ParseError,
                                                    "prediction decision fields are inconsistent", "decision");
    decision.authoritativeSnapshot = *snapshot;
    auto snapshotEnvelope = validateSnapshotEnvelope(decision.authoritativeSnapshot);
    if (!snapshotEnvelope)
        return eve::Result<ClimbingPredictionDecision>::failure(snapshotEnvelope.status());
    decision.extensionMetadata = unknownFields(root.value(),
        {"schemaId", "schemaVersion", "sequence", "clientTick", "serverTick", "disposition", "reason",
         "authoritativeCandidate", "authoritativeSnapshot"});
    return eve::Result<ClimbingPredictionDecision>::success(std::move(decision));
}

eve::Result<ClimbingPredictionDecision> ClimbingRuntime::tryBeginPredicted(
    physics::World3D& world, const ClimbingPose& pose, const ClimbingPredictionRequest& request,
    eve::SimulationTick serverTick, std::uint64_t maxClientTickLead) {
    ClimbingPredictionDecision decision;
    decision.sequence = request.sequence;
    decision.clientTick = request.clientTick;
    decision.serverTick = serverTick;
    const auto finish = [this](ClimbingPredictionDecision value)
        -> eve::Result<ClimbingPredictionDecision> {
        auto state = snapshot();
        if (!state) return eve::Result<ClimbingPredictionDecision>::failure(state.status());
        value.authoritativeSnapshot = std::move(state).takeValue();
        return eve::Result<ClimbingPredictionDecision>::success(std::move(value));
    };

    if (request.sequence.isZero() || !validKey(request.candidate)) {
        decision.disposition = ClimbingPredictionDisposition::Rejected;
        decision.reason = ClimbingPredictionReason::InvalidRequest;
        return finish(std::move(decision));
    }
    if (request.clientTick > serverTick &&
        request.clientTick.value() - serverTick.value() > maxClientTickLead) {
        decision.disposition = ClimbingPredictionDisposition::Rejected;
        decision.reason = ClimbingPredictionReason::TickTooFarAhead;
        return finish(std::move(decision));
    }
    if (activePhase(phase_)) {
        decision.disposition = ClimbingPredictionDisposition::Rejected;
        decision.reason = ClimbingPredictionReason::RuntimeBusy;
        return finish(std::move(decision));
    }

    auto candidates = probe(world, pose);
    if (!candidates) return eve::Result<ClimbingPredictionDecision>::failure(candidates.status());
    if (candidates.value().empty()) {
        decision.disposition = ClimbingPredictionDisposition::Rejected;
        decision.reason = ClimbingPredictionReason::NoCandidate;
        return finish(std::move(decision));
    }
    decision.authoritativeCandidate = makeClimbingCandidateKey(candidates.value().front(), 0);
    std::size_t match = candidates.value().size();
    for (std::size_t index = 0; index < candidates.value().size(); ++index) {
        if (index > std::numeric_limits<std::uint32_t>::max()) break;
        if (makeClimbingCandidateKey(candidates.value()[index], static_cast<std::uint32_t>(index)) ==
            request.candidate) {
            match = index;
            break;
        }
    }
    if (match == candidates.value().size()) {
        decision.disposition = ClimbingPredictionDisposition::Corrected;
        decision.reason = ClimbingPredictionReason::CandidateMismatch;
        return finish(std::move(decision));
    }

    decision.authoritativeCandidate = request.candidate;
    auto prepared = prepareBeginCandidate(world, pose, serverTick, candidates.value()[match]);
    if (!prepared) return eve::Result<ClimbingPredictionDecision>::failure(prepared.status());
    auto committed = commitBegin(std::move(prepared).takeValue());
    if (!committed) return eve::Result<ClimbingPredictionDecision>::failure(committed.status());
    decision.disposition = ClimbingPredictionDisposition::Accepted;
    decision.reason = ClimbingPredictionReason::None;
    return finish(std::move(decision));
}

}  // namespace eve::climbing
