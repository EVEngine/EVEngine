#include "settlement/Settlement.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::settlement {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

Value::Array strings(const std::vector<std::string>& values) {
    Value::Array encoded;
    encoded.reserve(values.size());
    for (const auto& value : values) encoded.emplace_back(value);
    return encoded;
}

Value encodeDecision(const SettlementRandomDecision& decision) {
    return Value::Object{{"accepted", decision.accepted},
                         {"sample", decision.sample},
                         {"sequence", std::to_string(decision.sequence)},
                         {"stream", decision.stream.format()},
                         {"threshold", decision.threshold}};
}

Value encodeRequest(const SettlementRequest& request) {
    Value::Array decisions;
    decisions.reserve(request.decisions.size());
    for (const auto& decision : request.decisions) decisions.push_back(encodeDecision(decision));

    return Value::Object{
        {"causation", request.causation.format()},
        {"causationKind", static_cast<std::int64_t>(request.causation.kind())},
        {"chain",
         Value::Object{{"depth", static_cast<std::int64_t>(request.chain.depth)},
                       {"emittedCount", static_cast<std::int64_t>(request.chain.emittedCount)},
                       {"triggerPath", strings(request.chain.triggerPath)}}},
        {"context", request.context},
        {"correlation", request.correlation.format()},
        {"correlationKind", static_cast<std::int64_t>(request.correlation.kind())},
        {"decisions", std::move(decisions)},
        {"kind", request.kind},
        {"magnitude", request.magnitude},
        {"resource", request.resource},
        {"source", request.source.format()},
        {"tags", strings(request.tags)},
        {"target", request.target.format()},
        {"tick", std::to_string(request.tick.value())},
        {"trace", static_cast<std::int64_t>(request.trace)},
        {"trigger", request.trigger},
    };
}

Value encodeStage(const SettlementStageResult& stage) {
    return Value::Object{{"after", stage.after},
                         {"before", stage.before},
                         {"details", stage.details},
                         {"kind", stageKindName(stage.kind)},
                         {"name", stage.name},
                         {"status", static_cast<std::int64_t>(stage.status)}};
}

eve::Result<Value> encodeEvent(const game_event::GameEvent& event) {
    auto payload = Value::fromJson(event.payload);
    if (!payload) return eve::Result<Value>::failure(payload.status());
    return eve::Result<Value>::success(Value::Object{
        {"causation", event.causation.format()},
        {"causationKind", static_cast<std::int64_t>(event.causation.kind())},
        {"correlation", event.correlation.format()},
        {"correlationKind", static_cast<std::int64_t>(event.correlation.kind())},
        {"flags", static_cast<std::int64_t>(event.flags)},
        {"payload", std::move(payload).takeValue()},
        {"schema", event.schemaId.format()},
        {"schemaVersion", std::to_string(event.schemaVersion.value())},
        {"source", event.source},
        {"subject", event.subject},
        {"tick", std::to_string(event.tick.value())},
        {"type", event.type},
    });
}

eve::Result<Value> encodeResult(const SettlementResult& result) {
    Value::Array stages;
    stages.reserve(result.stages.size());
    for (const auto& stage : result.stages) stages.push_back(encodeStage(stage));

    Value event;
    if (result.event) {
        auto encoded = encodeEvent(*result.event);
        if (!encoded) return eve::Result<Value>::failure(encoded.status());
        event = std::move(encoded).takeValue();
    }

    Value::Array derived;
    derived.reserve(result.derived.size());
    for (const auto& request : result.derived) derived.push_back(encodeRequest(request));

    return eve::Result<Value>::success(Value::Object{
        {"payload",
         Value::Object{{"absorbed", result.absorbed},
                       {"applied", result.applied},
                       {"clamped", result.clamped},
                       {"critical", result.critical},
                       {"derived", std::move(derived)},
                       {"disposition", settlementDispositionName(result.disposition)},
                       {"event", std::move(event)},
                       {"requested", result.requested},
                       {"resisted", result.resisted},
                       {"stages", std::move(stages)},
                       {"tick", std::to_string(result.tick.value())}}},
        {"schema", "settlement.result"},
        {"version", 1},
    });
}

template <class T>
void compareField(const T& expected, const T& actual, std::string path, std::vector<std::string>& differences) {
    if (expected != actual) differences.push_back(std::move(path));
}

eve::Result<const Value::Object*> strictObject(const Value& value, std::initializer_list<std::string_view> keys,
                                                const std::string& path) {
    const auto* object = value.getIf<Value::Object>();
    if (object == nullptr)
        return failure<const Value::Object*>(eve::DiagnosticCode::TypeMismatch, "expected object", path);
    if (object->size() != keys.size())
        return failure<const Value::Object*>(eve::DiagnosticCode::InvalidArgument,
                                             "object contains missing or unknown fields", path);
    for (const auto key : keys)
        if (!object->contains(std::string(key)))
            return failure<const Value::Object*>(eve::DiagnosticCode::InvalidArgument,
                                                 "object contains missing or unknown fields", path);
    return eve::Result<const Value::Object*>::success(object);
}

eve::Result<std::string> stringField(const Value::Object& object, std::string_view key, const std::string& path) {
    const auto* value = object.at(std::string(key)).getIf<std::string>();
    if (value == nullptr)
        return failure<std::string>(eve::DiagnosticCode::TypeMismatch, "expected string",
                                    path + "." + std::string(key));
    return eve::Result<std::string>::success(*value);
}

eve::Result<std::int64_t> integerField(const Value::Object& object, std::string_view key,
                                        const std::string& path) {
    const auto* value = object.at(std::string(key)).getIf<std::int64_t>();
    if (value == nullptr)
        return failure<std::int64_t>(eve::DiagnosticCode::TypeMismatch, "expected integer",
                                     path + "." + std::string(key));
    return eve::Result<std::int64_t>::success(*value);
}

eve::Result<double> numberField(const Value::Object& object, std::string_view key, const std::string& path) {
    const auto& field = object.at(std::string(key));
    if (const auto* value = field.getIf<double>()) return eve::Result<double>::success(*value);
    if (const auto* value = field.getIf<std::int64_t>())
        return eve::Result<double>::success(static_cast<double>(*value));
    return failure<double>(eve::DiagnosticCode::TypeMismatch, "expected number", path + "." + std::string(key));
}

eve::Result<std::uint64_t> unsignedText(std::string_view text, const std::string& path) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return failure<std::uint64_t>(eve::DiagnosticCode::ParseError, "expected unsigned decimal text", path);
    return eve::Result<std::uint64_t>::success(value);
}

eve::Result<std::vector<std::string>> stringArray(const Value& value, const std::string& path) {
    const auto* array = value.getIf<Value::Array>();
    if (array == nullptr)
        return failure<std::vector<std::string>>(eve::DiagnosticCode::TypeMismatch, "expected string array", path);
    std::vector<std::string> decoded;
    decoded.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* item = (*array)[index].getIf<std::string>();
        if (item == nullptr)
            return failure<std::vector<std::string>>(eve::DiagnosticCode::TypeMismatch, "expected string",
                                                     path + "[" + std::to_string(index) + "]");
        decoded.push_back(*item);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(decoded));
}

eve::Status locateRequestStatus(const eve::Status& status, const std::string& root) {
    std::vector<eve::Diagnostic> diagnostics;
    diagnostics.reserve(status.diagnostics().size());
    for (const auto& diagnostic : status.diagnostics()) {
        std::string path = root;
        if (!diagnostic.path().empty()) path += "." + diagnostic.path();
        diagnostics.emplace_back(diagnostic.code(), diagnostic.severity(), diagnostic.message(), std::move(path),
                                 diagnostic.details(), diagnostic.source());
    }
    return eve::Status(status.code(), std::move(diagnostics));
}

std::optional<StageKind> decodedStageKind(std::string_view value) noexcept {
    for (int index = 0; index <= int(StageKind::Event); ++index) {
        const auto kind = static_cast<StageKind>(index);
        if (value == stageKindName(kind)) return kind;
    }
    return std::nullopt;
}
std::optional<SettlementDisposition> decodedDisposition(std::string_view value) noexcept {
    for (int index = 0; index <= int(SettlementDisposition::InvalidTarget); ++index) {
        const auto disposition = static_cast<SettlementDisposition>(index);
        if (value == settlementDispositionName(disposition)) return disposition;
    }
    return std::nullopt;
}
bool knownStageKind(std::string_view value) noexcept { return decodedStageKind(value).has_value(); }
bool knownDisposition(std::string_view value) noexcept { return decodedDisposition(value).has_value(); }
eve::Result<game_event::CausationRef> decodeCausation(std::string_view text, std::int64_t kind,
                                                      const std::string& path) {
    using Ref = game_event::CausationRef;
    using R   = eve::Result<Ref>;
    if (kind == static_cast<std::int64_t>(Ref::Kind::Event)) {
        const auto id = game_event::EventId::parse(text);
        if (!id || id->isNil())
            return failure<Ref>(eve::DiagnosticCode::ParseError, "invalid causation event id", path + ".causation");
        return R::success(Ref::fromEventId(*id));
    }
    if (kind == static_cast<std::int64_t>(Ref::Kind::Command)) {
        const auto id = game_event::CommandId::parse(text);
        if (!id || id->isNil())
            return failure<Ref>(eve::DiagnosticCode::ParseError, "invalid causation command id", path + ".causation");
        return R::success(Ref::fromCommandId(*id));
    }
    if (kind != static_cast<std::int64_t>(Ref::Kind::None) || !text.empty())
        return failure<Ref>(eve::DiagnosticCode::InvalidArgument, "unsupported causation form",
                            path + ".causationKind");
    return R::success(Ref{});
}
eve::Result<game_event::CorrelationId> decodeCorrelation(std::string_view text, std::int64_t kind,
                                                         const std::string& path) {
    using Ref = game_event::CorrelationId;
    using R   = eve::Result<Ref>;
    if (kind == static_cast<std::int64_t>(Ref::Kind::Id)) {
        const auto id = game_event::EventId::parse(text);
        if (!id || id->isNil())
            return failure<Ref>(eve::DiagnosticCode::ParseError, "invalid correlation id", path + ".correlation");
        return R::success(Ref::fromEventId(*id));
    }
    if (kind != static_cast<std::int64_t>(Ref::Kind::None) || !text.empty())
        return failure<Ref>(eve::DiagnosticCode::InvalidArgument, "unsupported correlation form",
                            path + ".correlationKind");
    return R::success(Ref{});
}

eve::Result<SettlementRequest> decodeRequest(const Value& value, const std::string& path,
                                             bool emittedTemplate = false) {
    auto checked = strictObject(value,
                                {"causation", "causationKind", "chain", "context", "correlation",
                                 "correlationKind", "decisions", "kind", "magnitude", "resource", "source",
                                 "tags", "target", "tick", "trace", "trigger"},
                                path);
    if (!checked) return eve::Result<SettlementRequest>::failure(checked.status());
    const auto& object = *checked.value();

    SettlementRequest request;
    auto              sourceText = stringField(object, "source", path);
    if (!sourceText) return eve::Result<SettlementRequest>::failure(sourceText.status());
    auto targetText = stringField(object, "target", path);
    if (!targetText) return eve::Result<SettlementRequest>::failure(targetText.status());
    auto kind = stringField(object, "kind", path);
    if (!kind) return eve::Result<SettlementRequest>::failure(kind.status());
    auto resource = stringField(object, "resource", path);
    if (!resource) return eve::Result<SettlementRequest>::failure(resource.status());
    auto trigger = stringField(object, "trigger", path);
    if (!trigger) return eve::Result<SettlementRequest>::failure(trigger.status());
    auto tickText = stringField(object, "tick", path);
    if (!tickText) return eve::Result<SettlementRequest>::failure(tickText.status());
    auto magnitude = numberField(object, "magnitude", path);
    if (!magnitude) return eve::Result<SettlementRequest>::failure(magnitude.status());
    auto trace = integerField(object, "trace", path);
    if (!trace) return eve::Result<SettlementRequest>::failure(trace.status());

    const auto sourceId = eve::PersistentId::parse(sourceText.value());
    const auto targetId = eve::PersistentId::parse(targetText.value());
    if (!sourceId)
        return failure<SettlementRequest>(eve::DiagnosticCode::ParseError, "invalid source subject id",
                                          path + ".source");
    if (!targetId)
        return failure<SettlementRequest>(eve::DiagnosticCode::ParseError, "invalid target subject id",
                                          path + ".target");
    request.source    = SubjectRef::fromPersistentId(*sourceId);
    request.target    = SubjectRef::fromPersistentId(*targetId);
    request.kind      = std::move(kind).takeValue();
    request.resource  = std::move(resource).takeValue();
    request.trigger   = std::move(trigger).takeValue();
    request.magnitude = magnitude.value();
    if (trace.value() < 0 || trace.value() > static_cast<std::int64_t>(SettlementTraceLevel::Full))
        return failure<SettlementRequest>(eve::DiagnosticCode::InvalidArgument, "unknown trace level",
                                          path + ".trace");
    request.trace = static_cast<SettlementTraceLevel>(trace.value());
    auto tick = unsignedText(tickText.value(), path + ".tick");
    if (!tick) return eve::Result<SettlementRequest>::failure(tick.status());
    request.tick = SimulationTick(tick.value());
    request.context = object.at("context");

    auto tags = stringArray(object.at("tags"), path + ".tags");
    if (!tags) return eve::Result<SettlementRequest>::failure(tags.status());
    request.tags = std::move(tags).takeValue();

    auto chainObject = strictObject(object.at("chain"), {"depth", "emittedCount", "triggerPath"}, path + ".chain");
    if (!chainObject) return eve::Result<SettlementRequest>::failure(chainObject.status());
    auto depth = integerField(*chainObject.value(), "depth", path + ".chain");
    if (!depth) return eve::Result<SettlementRequest>::failure(depth.status());
    auto emitted = integerField(*chainObject.value(), "emittedCount", path + ".chain");
    if (!emitted) return eve::Result<SettlementRequest>::failure(emitted.status());
    if (depth.value() < 0 || depth.value() > std::numeric_limits<std::uint32_t>::max() || emitted.value() < 0 ||
        emitted.value() > std::numeric_limits<std::uint32_t>::max())
        return failure<SettlementRequest>(eve::DiagnosticCode::InvalidArgument, "chain counter is out of range",
                                          path + ".chain");
    request.chain.depth        = static_cast<std::uint32_t>(depth.value());
    request.chain.emittedCount = static_cast<std::uint32_t>(emitted.value());
    auto triggerPath = stringArray(chainObject.value()->at("triggerPath"), path + ".chain.triggerPath");
    if (!triggerPath) return eve::Result<SettlementRequest>::failure(triggerPath.status());
    request.chain.triggerPath = std::move(triggerPath).takeValue();

    const auto* decisions = object.at("decisions").getIf<Value::Array>();
    if (decisions == nullptr)
        return failure<SettlementRequest>(eve::DiagnosticCode::TypeMismatch, "expected decision array",
                                          path + ".decisions");
    request.decisions.reserve(decisions->size());
    for (std::size_t index = 0; index < decisions->size(); ++index) {
        const auto itemPath = path + ".decisions[" + std::to_string(index) + "]";
        auto decisionObject = strictObject((*decisions)[index], {"accepted", "sample", "sequence", "stream",
                                                                  "threshold"}, itemPath);
        if (!decisionObject) return eve::Result<SettlementRequest>::failure(decisionObject.status());
        auto streamText = stringField(*decisionObject.value(), "stream", itemPath);
        if (!streamText) return eve::Result<SettlementRequest>::failure(streamText.status());
        auto sequenceText = stringField(*decisionObject.value(), "sequence", itemPath);
        if (!sequenceText) return eve::Result<SettlementRequest>::failure(sequenceText.status());
        auto sample = numberField(*decisionObject.value(), "sample", itemPath);
        if (!sample) return eve::Result<SettlementRequest>::failure(sample.status());
        auto threshold = numberField(*decisionObject.value(), "threshold", itemPath);
        if (!threshold) return eve::Result<SettlementRequest>::failure(threshold.status());
        const auto* accepted = decisionObject.value()->at("accepted").getIf<bool>();

        if (accepted == nullptr)
            return failure<SettlementRequest>(eve::DiagnosticCode::TypeMismatch, "expected boolean",
                                              itemPath + ".accepted");
        const auto stream = LogicalId::parse(streamText.value());
        if (!stream)
            return failure<SettlementRequest>(eve::DiagnosticCode::ParseError, "invalid decision stream id",
                                              itemPath + ".stream");
        auto sequence = unsignedText(sequenceText.value(), itemPath + ".sequence");
        if (!sequence) return eve::Result<SettlementRequest>::failure(sequence.status());
        request.decisions.push_back({*stream, sequence.value(), sample.value(), threshold.value(), *accepted});
    }

    auto causationText = stringField(object, "causation", path);
    if (!causationText) return eve::Result<SettlementRequest>::failure(causationText.status());
    auto causationKind = integerField(object, "causationKind", path);
    if (!causationKind) return eve::Result<SettlementRequest>::failure(causationKind.status());
    auto correlationText = stringField(object, "correlation", path);
    if (!correlationText) return eve::Result<SettlementRequest>::failure(correlationText.status());
    auto correlationKind = integerField(object, "correlationKind", path);
    if (!correlationKind) return eve::Result<SettlementRequest>::failure(correlationKind.status());
    auto causation = decodeCausation(causationText.value(), causationKind.value(), path);
    if (!causation) return eve::Result<SettlementRequest>::failure(causation.status());
    request.causation = std::move(causation).takeValue();
    auto correlation  = decodeCorrelation(correlationText.value(), correlationKind.value(), path);
    if (!correlation) return eve::Result<SettlementRequest>::failure(correlation.status());
    request.correlation = std::move(correlation).takeValue();
    auto executable = request;
    if (emittedTemplate && executable.chain.depth == 0 && !executable.trigger.empty()) {
        executable.chain.depth = 1;
        executable.chain.emittedCount = 1;
        executable.chain.triggerPath = {executable.trigger};
    }
    auto valid = validateSettlementRequest(executable);
    if (!valid) return eve::Result<SettlementRequest>::failure(locateRequestStatus(valid.status(), path));
    return eve::Result<SettlementRequest>::success(std::move(request));
}

eve::Result<void> validateResultShape(const Value& value, const std::string& path) {
    auto result = strictObject(value, {"payload", "schema", "version"}, path);
    if (!result) return eve::Result<void>::failure(result.status());
    auto schema = stringField(*result.value(), "schema", path);
    if (!schema) return eve::Result<void>::failure(schema.status());
    auto version = integerField(*result.value(), "version", path);
    if (!version) return eve::Result<void>::failure(version.status());
    if (schema.value() != "settlement.result")
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "unexpected settlement result schema",
                             path + ".schema");
    if (version.value() != 1)
        return failure<void>(eve::DiagnosticCode::UnknownVersion, "unsupported settlement result version",
                             path + ".version");

    auto payload = strictObject(result.value()->at("payload"),
                                {"absorbed", "applied", "clamped", "critical", "derived", "disposition", "event",
                                 "requested", "resisted", "stages", "tick"},
                                path + ".payload");
    if (!payload) return eve::Result<void>::failure(payload.status());
    for (const auto field : {"requested", "applied", "absorbed", "resisted", "clamped"}) {
        auto number = numberField(*payload.value(), field, path + ".payload");
        if (!number) return eve::Result<void>::failure(number.status());
        if (!std::isfinite(number.value()) || number.value() < 0.0)
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "expected finite non-negative number",
                                 path + ".payload." + field);
    }
    if (payload.value()->at("critical").getIf<bool>() == nullptr)
        return failure<void>(eve::DiagnosticCode::TypeMismatch, "expected boolean", path + ".payload.critical");
    auto disposition = stringField(*payload.value(), "disposition", path + ".payload");
    if (!disposition) return eve::Result<void>::failure(disposition.status());
    if (!knownDisposition(disposition.value()))
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown settlement disposition",
                             path + ".payload.disposition");
    auto tickText = stringField(*payload.value(), "tick", path + ".payload");
    if (!tickText) return eve::Result<void>::failure(tickText.status());
    auto tick = unsignedText(tickText.value(), path + ".payload.tick");
    if (!tick) return eve::Result<void>::failure(tick.status());
    const auto* stages = payload.value()->at("stages").getIf<Value::Array>();
    if (stages == nullptr)
        return failure<void>(eve::DiagnosticCode::TypeMismatch, "expected stage array", path + ".payload.stages");
    for (std::size_t index = 0; index < stages->size(); ++index) {
        auto stage = strictObject((*stages)[index], {"after", "before", "details", "kind", "name", "status"},
                                  path + ".payload.stages[" + std::to_string(index) + "]");
        if (!stage) return eve::Result<void>::failure(stage.status());
        const auto stagePath = path + ".payload.stages[" + std::to_string(index) + "]";
        auto       kind      = stringField(*stage.value(), "kind", stagePath);
        if (!kind) return eve::Result<void>::failure(kind.status());
        auto name = stringField(*stage.value(), "name", stagePath);
        if (!name) return eve::Result<void>::failure(name.status());
        auto status = integerField(*stage.value(), "status", stagePath);
        if (!status) return eve::Result<void>::failure(status.status());
        auto before = numberField(*stage.value(), "before", stagePath);
        if (!before) return eve::Result<void>::failure(before.status());
        auto after = numberField(*stage.value(), "after", stagePath);
        if (!after) return eve::Result<void>::failure(after.status());
        if (!knownStageKind(kind.value()))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown settlement stage kind",
                                 stagePath + ".kind");
        if (status.value() < 0 || status.value() > static_cast<std::int64_t>(eve::StatusCode::Failed))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown settlement stage status",
                                 stagePath + ".status");
        if (!std::isfinite(before.value()) || before.value() < 0.0 || !std::isfinite(after.value()) ||
            after.value() < 0.0)
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "stage magnitudes must be finite and non-negative", stagePath);
        if (stage.value()->at("details").getIf<Value::Object>() == nullptr)
            return failure<void>(eve::DiagnosticCode::TypeMismatch, "expected details object",
                                 stagePath + ".details");
    }
    const auto* derived = payload.value()->at("derived").getIf<Value::Array>();
    if (derived == nullptr)
        return failure<void>(eve::DiagnosticCode::TypeMismatch, "expected derived request array",
                             path + ".payload.derived");
    for (std::size_t index = 0; index < derived->size(); ++index) {
        auto request = decodeRequest((*derived)[index], path + ".payload.derived[" + std::to_string(index) + "]",
                                     true);
        if (!request) return eve::Result<void>::failure(request.status());
    }
    const auto& event = payload.value()->at("event");
    if (!event.isNull()) {
        auto checked = strictObject(event,
                                    {"causation", "causationKind", "correlation", "correlationKind", "flags",
                                     "payload", "schema", "schemaVersion", "source", "subject", "tick", "type"},
                                    path + ".payload.event");
        if (!checked) return eve::Result<void>::failure(checked.status());
        const auto eventPath = path + ".payload.event";
        for (const auto field : {"causation", "correlation", "schema", "schemaVersion", "source", "subject",
                                 "tick", "type"}) {
            auto text = stringField(*checked.value(), field, eventPath);
            if (!text) return eve::Result<void>::failure(text.status());
        }
        for (const auto field : {"causationKind", "correlationKind", "flags"}) {
            auto integer = integerField(*checked.value(), field, eventPath);
            if (!integer) return eve::Result<void>::failure(integer.status());
            if (integer.value() < 0)
                return failure<void>(eve::DiagnosticCode::InvalidArgument, "expected non-negative integer",
                                     eventPath + "." + field);
        }
        const auto causationKind = checked.value()->at("causationKind").asInt();
        const auto correlationKind = checked.value()->at("correlationKind").asInt();
        if (causationKind > static_cast<std::int64_t>(game_event::CausationRef::Kind::Command))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown event causation kind",
                                 eventPath + ".causationKind");
        if (correlationKind > static_cast<std::int64_t>(game_event::CorrelationId::Kind::Id))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown event correlation kind",
                                 eventPath + ".correlationKind");
        for (const auto field : {"schemaVersion", "tick"}) {
            auto text = stringField(*checked.value(), field, eventPath);
            if (!text) return eve::Result<void>::failure(text.status());
            auto number = unsignedText(text.value(), eventPath + "." + field);
            if (!number) return eve::Result<void>::failure(number.status());
        }
        const auto& object    = *checked.value();
        auto        causation = decodeCausation(*object.at("causation").getIf<std::string>(), causationKind, eventPath);
        if (!causation) return eve::Result<void>::failure(causation.status());
        auto correlation =
            decodeCorrelation(*object.at("correlation").getIf<std::string>(), correlationKind, eventPath);
        if (!correlation) return eve::Result<void>::failure(correlation.status());
        const auto& schema = *object.at("schema").getIf<std::string>();
        if (!schema.empty() && !game_event::SchemaId::parse(schema))
            return failure<void>(eve::DiagnosticCode::ParseError, "invalid event schema", eventPath + ".schema");
        if (object.at("flags").asInt() > std::numeric_limits<std::uint32_t>::max())
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "event flags exceed uint32",
                                 eventPath + ".flags");
        if (checked.value()->at("payload").getIf<Value::Object>() == nullptr)
            return failure<void>(eve::DiagnosticCode::TypeMismatch, "expected event payload object",
                                 eventPath + ".payload");
    }
    return eve::Result<void>::success();
}

eve::Result<SettlementResult> decodeResult(const Value& value, const std::string& path) {
    using R    = eve::Result<SettlementResult>;
    auto valid = validateResultShape(value, path);
    if (!valid) return R::failure(valid.status());
    const auto& payload = *value.find("payload")->getIf<Value::Object>();
    const auto  number  = [](const Value& value) {
        return value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
    };
    SettlementResult result;
    result.requested   = number(payload.at("requested"));
    result.applied     = number(payload.at("applied"));
    result.absorbed    = number(payload.at("absorbed"));
    result.resisted    = number(payload.at("resisted"));
    result.clamped     = number(payload.at("clamped"));
    result.critical    = *payload.at("critical").getIf<bool>();
    result.disposition = *decodedDisposition(*payload.at("disposition").getIf<std::string>());
    auto tick          = unsignedText(*payload.at("tick").getIf<std::string>(), path + ".payload.tick");
    if (!tick) return R::failure(tick.status());
    result.tick = SimulationTick(tick.value());
    for (const auto& value : *payload.at("stages").getIf<Value::Array>()) {
        const auto& row = *value.getIf<Value::Object>();
        result.stages.push_back({*decodedStageKind(*row.at("kind").getIf<std::string>()),
                                 *row.at("name").getIf<std::string>(),
                                 static_cast<eve::StatusCode>(row.at("status").asInt()), number(row.at("before")),
                                 number(row.at("after")), row.at("details")});
    }
    for (const auto& value : *payload.at("derived").getIf<Value::Array>()) {
        auto request =
            decodeRequest(value, path + ".payload.derived[" + std::to_string(result.derived.size()) + "]", true);
        if (!request) return R::failure(request.status());
        result.derived.push_back(std::move(request).takeValue());
    }
    if (!payload.at("event").isNull()) {
        const auto& row       = *payload.at("event").getIf<Value::Object>();
        const auto  eventPath = path + ".payload.event";
        const auto  text = [&](const char* key) -> const std::string& { return *row.at(key).getIf<std::string>(); };
        game_event::GameEvent event;
        event.type     = text("type");
        event.source   = text("source");
        event.subject  = text("subject");
        auto causation = decodeCausation(text("causation"), row.at("causationKind").asInt(), eventPath);
        if (!causation) return R::failure(causation.status());
        event.causation  = std::move(causation).takeValue();
        auto correlation = decodeCorrelation(text("correlation"), row.at("correlationKind").asInt(), eventPath);
        if (!correlation) return R::failure(correlation.status());
        event.correlation = std::move(correlation).takeValue();
        if (!text("schema").empty()) {
            auto schema = game_event::SchemaId::parse(text("schema"));
            if (!schema)
                return failure<SettlementResult>(eve::DiagnosticCode::ParseError, "invalid event schema",
                                                 eventPath + ".schema");
            event.schemaId = *schema;
        }
        auto version = unsignedText(text("schemaVersion"), eventPath + ".schemaVersion");
        if (!version) return R::failure(version.status());
        event.schemaVersion = SchemaVersion(version.value());
        auto eventTick      = unsignedText(text("tick"), eventPath + ".tick");
        if (!eventTick) return R::failure(eventTick.status());
        event.tick       = SimulationTick(eventTick.value());
        const auto flags = row.at("flags").asInt();
        if (flags > std::numeric_limits<std::uint32_t>::max())
            return failure<SettlementResult>(eve::DiagnosticCode::InvalidArgument, "event flags exceed uint32",
                                             eventPath + ".flags");
        event.flags       = static_cast<std::uint32_t>(flags);
        auto eventPayload = row.at("payload").toJson();
        if (!eventPayload) return R::failure(eventPayload.status());
        event.payload = std::move(eventPayload).takeValue();
        result.event.emplace(std::move(event));
    }
    return R::success(std::move(result));
}

eve::Result<Value> parseReplayRecord(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return eve::Result<Value>::failure(parsed.status());
    auto root = std::move(parsed).takeValue();
    auto checked = strictObject(root, {"request", "result", "resultDigest", "ruleDigest", "schema", "version"},
                                "record");
    if (!checked) return eve::Result<Value>::failure(checked.status());
    auto schema = stringField(*checked.value(), "schema", "record");
    if (!schema) return eve::Result<Value>::failure(schema.status());
    auto version = integerField(*checked.value(), "version", "record");
    if (!version) return eve::Result<Value>::failure(version.status());
    if (schema.value() != "settlement.replay")
        return failure<Value>(eve::DiagnosticCode::InvalidArgument, "unexpected replay schema", "record.schema");
    if (version.value() != 1)
        return failure<Value>(eve::DiagnosticCode::UnknownVersion, "unsupported settlement replay version",
                              "record.version");
    for (const auto field : {"ruleDigest", "resultDigest"}) {
        auto digestText = stringField(*checked.value(), field, "record");
        if (!digestText) return eve::Result<Value>::failure(digestText.status());
        const auto digest = eve::ContentId::parse(digestText.value());
        if (!digest || digest->isNil())
            return failure<Value>(eve::DiagnosticCode::ParseError, "invalid content digest",
                                  "record." + std::string(field));
    }
    auto request = decodeRequest(checked.value()->at("request"), "record.request");
    if (!request) return eve::Result<Value>::failure(request.status());
    auto result = validateResultShape(checked.value()->at("result"), "record.result");
    if (!result) return eve::Result<Value>::failure(result.status());
    return eve::Result<Value>::success(std::move(root));
}

bool numericEquivalent(const Value& left, const Value& right) {
    if (!left.isNumeric() || !right.isNumeric()) return false;
    const auto number = [](const Value& value) {
        if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
        return *value.getIf<double>();
    };
    return number(left) == number(right);
}

void diffValues(const Value& expected, const Value& actual, const std::string& path,
                std::vector<std::string>& differences) {
    if (numericEquivalent(expected, actual)) return;
    if (expected.type() != actual.type()) {
        differences.push_back(path);
        return;
    }
    if (const auto* expectedObject = expected.getIf<Value::Object>()) {
        const auto* actualObject = actual.getIf<Value::Object>();
        for (const auto& [key, value] : *expectedObject) {
            const auto found = actualObject->find(key);
            if (found == actualObject->end()) differences.push_back(path + "." + key);
            else diffValues(value, found->second, path + "." + key, differences);
        }
        for (const auto& [key, value] : *actualObject)
            if (!expectedObject->contains(key)) differences.push_back(path + "." + key);
        return;
    }
    if (const auto* expectedArray = expected.getIf<Value::Array>()) {
        const auto* actualArray = actual.getIf<Value::Array>();
        if (expectedArray->size() != actualArray->size()) differences.push_back(path + ".size");
        const auto count = std::min(expectedArray->size(), actualArray->size());
        for (std::size_t index = 0; index < count; ++index)
            diffValues((*expectedArray)[index], (*actualArray)[index],
                       path + "[" + std::to_string(index) + "]", differences);
        return;
    }
    if (expected != actual) differences.push_back(path);
}

}  // namespace

eve::Result<std::string> settlementRequestCanonicalJson(const SettlementRequest& request) {
    auto payload  = encodeRequest(request);
    auto admitted = decodeRequest(payload, "request.payload");
    if (!admitted) return eve::Result<std::string>::failure(admitted.status());
    return Value(Value::Object{{"schema", "settlement.request"}, {"version", 1}, {"payload", std::move(payload)}})
        .toJson();
}
eve::Result<SettlementRequest> settlementRequestFromJson(std::string_view json) {
    using R     = eve::Result<SettlementRequest>;
    auto parsed = Value::fromJson(json);
    if (!parsed) return R::failure(parsed.status());
    auto root = strictObject(parsed.value(), {"schema", "version", "payload"}, "request");
    if (!root) return R::failure(root.status());
    auto schema = stringField(*root.value(), "schema", "request");
    if (!schema) return R::failure(schema.status());
    auto version = integerField(*root.value(), "version", "request");
    if (!version) return R::failure(version.status());
    if (schema.value() != "settlement.request")
        return failure<SettlementRequest>(eve::DiagnosticCode::InvalidArgument, "unexpected request schema",
                                          "request.schema");
    if (version.value() != 1)
        return failure<SettlementRequest>(eve::DiagnosticCode::UnknownVersion, "unknown request version",
                                          "request.version");
    return decodeRequest(root.value()->at("payload"), "request.payload");
}

eve::Result<std::string> settlementResultCanonicalJson(const SettlementResult& result) {
    auto encoded = encodeResult(result);
    if (!encoded) return eve::Result<std::string>::failure(encoded.status());
    return std::move(encoded).takeValue().toJson();
}

eve::Result<SettlementResult> settlementResultFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return eve::Result<SettlementResult>::failure(parsed.status());
    return decodeResult(parsed.value(), "result");
}

eve::Result<eve::ContentId> settlementResultDigest(const SettlementResult&           result,
                                                    const eve::SnapshotHashProvider& hashProvider) {
    if (!hashProvider)
        return failure<eve::ContentId>(eve::DiagnosticCode::PreconditionViolation,
                                       "settlement result digest requires a hash provider", "hashProvider");
    auto canonical = settlementResultCanonicalJson(result);
    if (!canonical) return eve::Result<eve::ContentId>::failure(canonical.status());
    return hashProvider(canonical.value());
}

eve::Result<std::vector<std::string>> verifySettlementResult(const SettlementResult& expected,
                                                               const SettlementResult& actual) {
    std::vector<std::string> differences;
    compareField(expected.requested, actual.requested, "requested", differences);
    compareField(expected.applied, actual.applied, "applied", differences);
    compareField(expected.absorbed, actual.absorbed, "absorbed", differences);
    compareField(expected.resisted, actual.resisted, "resisted", differences);
    compareField(expected.clamped, actual.clamped, "clamped", differences);
    compareField(expected.critical, actual.critical, "critical", differences);
    compareField(expected.disposition, actual.disposition, "disposition", differences);
    compareField(expected.tick, actual.tick, "tick", differences);

    if (expected.stages.size() != actual.stages.size()) differences.emplace_back("stages.size");
    const auto stageCount = std::min(expected.stages.size(), actual.stages.size());
    for (std::size_t index = 0; index < stageCount; ++index) {
        const auto path = "stages[" + std::to_string(index) + "]";
        compareField(expected.stages[index].kind, actual.stages[index].kind, path + ".kind", differences);
        compareField(expected.stages[index].name, actual.stages[index].name, path + ".name", differences);
        compareField(expected.stages[index].status, actual.stages[index].status, path + ".status", differences);
        compareField(expected.stages[index].before, actual.stages[index].before, path + ".before", differences);
        compareField(expected.stages[index].after, actual.stages[index].after, path + ".after", differences);
        compareField(expected.stages[index].details, actual.stages[index].details, path + ".details", differences);
    }

    if (expected.event.has_value() != actual.event.has_value()) {
        differences.emplace_back("event");
    } else if (expected.event) {
        auto expectedEvent = encodeEvent(*expected.event);
        if (!expectedEvent) return eve::Result<std::vector<std::string>>::failure(expectedEvent.status());
        auto actualEvent = encodeEvent(*actual.event);
        if (!actualEvent) return eve::Result<std::vector<std::string>>::failure(actualEvent.status());
        if (expectedEvent.value() != actualEvent.value()) differences.emplace_back("event");
    }

    if (expected.derived.size() != actual.derived.size()) differences.emplace_back("derived.size");
    const auto derivedCount = std::min(expected.derived.size(), actual.derived.size());
    for (std::size_t index = 0; index < derivedCount; ++index)
        if (encodeRequest(expected.derived[index]) != encodeRequest(actual.derived[index]))
            differences.push_back("derived[" + std::to_string(index) + "]");

    const auto code = differences.empty() ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<std::vector<std::string>>::success(std::move(differences), eve::Status::success(code));
}

eve::Result<std::string> createSettlementReplayRecord(const SettlementRequest& request, eve::ContentId ruleDigest,
                                                       const SettlementResult&           result,
                                                       const eve::SnapshotHashProvider& hashProvider) {
    if (ruleDigest.isNil())
        return failure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                    "settlement replay requires a non-nil rule digest", "ruleDigest");
    if (!hashProvider)
        return failure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                    "settlement replay requires a hash provider", "hashProvider");
    auto encodedResult = encodeResult(result);
    if (!encodedResult) return eve::Result<std::string>::failure(encodedResult.status());
    auto resultJson = encodedResult.value().toJson();
    if (!resultJson) return eve::Result<std::string>::failure(resultJson.status());
    auto resultDigest = hashProvider(resultJson.value());
    if (!resultDigest) return eve::Result<std::string>::failure(resultDigest.status());
    return Value(Value::Object{
                     {"request", encodeRequest(request)},
                     {"result", std::move(encodedResult).takeValue()},
                     {"resultDigest", resultDigest.value().format()},
                     {"ruleDigest", ruleDigest.format()},
                     {"schema", "settlement.replay"},
                     {"version", 1},
                 })
        .toJson();
}

eve::Result<SettlementRequest> settlementReplayRequest(std::string_view recordJson) {
    auto record = parseReplayRecord(recordJson);
    if (!record) return eve::Result<SettlementRequest>::failure(record.status());
    const auto& root = *record.value().getIf<Value::Object>();
    return decodeRequest(root.at("request"), "record.request");
}

eve::Result<std::vector<std::string>> verifySettlementReplayRecord(
    std::string_view recordJson, eve::ContentId ruleDigest, const SettlementResult& actual,
    const eve::SnapshotHashProvider& hashProvider) {
    if (!hashProvider)
        return failure<std::vector<std::string>>(eve::DiagnosticCode::PreconditionViolation,
                                                 "settlement replay verification requires a hash provider",
                                                 "hashProvider");
    auto record = parseReplayRecord(recordJson);
    if (!record) return eve::Result<std::vector<std::string>>::failure(record.status());
    const auto& root = *record.value().getIf<Value::Object>();

    auto storedRuleText = stringField(root, "ruleDigest", "record");
    if (!storedRuleText) return eve::Result<std::vector<std::string>>::failure(storedRuleText.status());
    auto storedResultText = stringField(root, "resultDigest", "record");
    if (!storedResultText) return eve::Result<std::vector<std::string>>::failure(storedResultText.status());
    const auto storedRule   = eve::ContentId::parse(storedRuleText.value());
    const auto storedResult = eve::ContentId::parse(storedResultText.value());
    if (!storedRule)
        return failure<std::vector<std::string>>(eve::DiagnosticCode::ParseError, "invalid rule digest",
                                                 "record.ruleDigest");
    if (!storedResult)
        return failure<std::vector<std::string>>(eve::DiagnosticCode::ParseError, "invalid result digest",
                                                 "record.resultDigest");

    auto expectedJson = root.at("result").toJson();
    if (!expectedJson) return eve::Result<std::vector<std::string>>::failure(expectedJson.status());
    auto verifiedDigest = hashProvider(expectedJson.value());
    if (!verifiedDigest) return eve::Result<std::vector<std::string>>::failure(verifiedDigest.status());
    if (verifiedDigest.value() != *storedResult)
        return failure<std::vector<std::string>>(eve::DiagnosticCode::HashMismatch,
                                                 "settlement replay result payload digest mismatch",
                                                 "record.resultDigest");

    auto encodedActual = encodeResult(actual);
    if (!encodedActual) return eve::Result<std::vector<std::string>>::failure(encodedActual.status());
    std::vector<std::string> differences;
    if (*storedRule != ruleDigest) differences.emplace_back("ruleDigest");
    diffValues(root.at("result"), encodedActual.value(), "result", differences);
    const auto code = differences.empty() ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<std::vector<std::string>>::success(std::move(differences), eve::Status::success(code));
}

}  // namespace eve::settlement
