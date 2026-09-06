#include "devtools/PlayTrace.h"

#include <cstdio>
#include <set>
#include <utility>

namespace eve::dev {
namespace {

constexpr std::string_view kTraceSchemaId = "evengine.play-trace";
constexpr std::int64_t     kTraceVersion  = 1;

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> knownFields(const Value::Object& object, const std::set<std::string>& allowed,
                         std::string_view path) {
    for (const auto& [name, value] : object) {
        (void)value;
        if (!allowed.contains(name))
            return failure<void>(DiagnosticCode::ParseError, "unknown play-trace field",
                                 std::string(path) + "." + name);
    }
    return Result<void>::success();
}

Result<const Value::Object*> asObject(const Value& value, std::string path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object)
        return failure<const Value::Object*>(DiagnosticCode::ParseError, "play-trace value must be an object",
                                             std::move(path));
    return Result<const Value::Object*>::success(object);
}

std::string hex64(std::uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

Result<std::string> stringField(const Value::Object& object, std::string_view name, std::string_view path) {
    const auto found = object.find(std::string(name));
    if (found == object.end() || !found->second.isString())
        return failure<std::string>(DiagnosticCode::ParseError, "missing play-trace string",
                                    std::string(path) + "." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

}  // namespace

Result<std::string> playObservationDigest(const Value& state) {
    auto json = state.toJson();
    if (!json) return Result<std::string>::failure(json.status());
    return Result<std::string>::success("fnv1a64:" + hex64(fnv1a64(json.value())));
}

Result<Value> parsePlayTrace(const Value& recording) {
    auto root = asObject(recording, "recording");
    if (!root) return Result<Value>::failure(root.status());
    auto known = knownFields(*root.value(),
                             {"contractHash", "contractId", "engine", "result", "schemaId", "schemaVersion", "seed",
                              "startCheckpoint", "steps"},
                             "recording");
    if (!known) return Result<Value>::failure(known.status());
    auto schema = stringField(*root.value(), "schemaId", "recording");
    if (!schema) return Result<Value>::failure(schema.status());
    const auto version = root.value()->find("schemaVersion");
    if (version == root.value()->end() || !version->second.isInt64() || version->second.asInt() != kTraceVersion)
        return failure<Value>(DiagnosticCode::Unsupported, "unsupported play-trace schema", "recording.schemaVersion");
    if (schema.value() != kTraceSchemaId)
        return failure<Value>(DiagnosticCode::Unsupported, "unsupported play-trace schema", "recording.schemaId");
    const auto steps = root.value()->find("steps");
    if (steps == root.value()->end() || !steps->second.isArray())
        return failure<Value>(DiagnosticCode::ParseError, "play-trace requires steps", "recording.steps");
    const auto* array = steps->second.getIf<Value::Array>();
    for (std::size_t index = 0; index < array->size(); ++index) {
        const std::string itemPath = "recording.steps." + std::to_string(index);
        auto item = asObject((*array)[index], itemPath);
        if (!item) return Result<Value>::failure(item.status());
        auto itemKnown =
            knownFields(*item.value(), {"capture", "hostFrame", "observationDigest", "request", "seq"}, itemPath);
        if (!itemKnown) return Result<Value>::failure(itemKnown.status());
        const auto request = item.value()->find("request");
        if (request == item.value()->end())
            return failure<Value>(DiagnosticCode::ParseError, "trace step requires request", itemPath + ".request");
    }
    return Result<Value>::success(recording);
}

PlayReplayGuard::PlayReplayGuard() { PlayTraceBuffer::instance().setReplaying(true); }
PlayReplayGuard::~PlayReplayGuard() { PlayTraceBuffer::instance().setReplaying(false); }

Result<Value> replayPlayTrace(const Value& recording, IPlayHostRuntime& runtime) {
    auto parsed = parsePlayTrace(recording);
    if (!parsed) return Result<Value>::failure(parsed.status());
    auto root = asObject(parsed.value(), "recording");
    if (!root) return Result<Value>::failure(root.status());
    const auto checkpoint = root.value()->find("startCheckpoint");
    if (checkpoint != root.value()->end() && checkpoint->second.isString() &&
        !checkpoint->second.asString().empty()) {
        auto restored = runtime.restoreCheckpoint(checkpoint->second.asString());
        if (!restored) return Result<Value>::failure(restored.status());
    }
    PlayReplayGuard guard;
    const auto* steps = root.value()->find("steps")->second.getIf<Value::Array>();
    for (std::size_t index = 0; index < steps->size(); ++index) {
        const auto* step = (*steps)[index].getIf<Value::Object>();
        if (!step)
            return failure<Value>(DiagnosticCode::ParseError, "trace step must be an object",
                                  "recording.steps." + std::to_string(index));
        auto replayed = executePlayRequest(step->at("request"), runtime);
        if (!replayed) return Result<Value>::failure(replayed.status());
        const auto digest = step->find("observationDigest");
        if (digest != step->end() && digest->second.isString() && !digest->second.asString().empty()) {
            const auto* response = replayed.value().getIf<Value::Object>();
            if (!response)
                return failure<Value>(DiagnosticCode::Failed, "replay response must be an object",
                                      "recording.steps." + std::to_string(index));
            const auto state = response->find("state");
            if (state == response->end())
                return failure<Value>(DiagnosticCode::HashMismatch, "replay observe is missing state",
                                      "recording.steps." + std::to_string(index));
            auto actual = playObservationDigest(state->second);
            if (!actual) return Result<Value>::failure(actual.status());
            if (actual.value() != digest->second.asString())
                return failure<Value>(DiagnosticCode::HashMismatch, "play-trace observation digest mismatch",
                                      "recording.steps." + std::to_string(index));
        }
    }
    return Result<Value>::success(Value(Value::Object{
        {"op", Value("replay")},
        {"schemaId", Value("evengine.play-response")},
        {"schemaVersion", Value(std::int64_t{1})},
        {"status", Value("passed")},
        {"steps", Value(static_cast<std::int64_t>(steps->size()))},
    }));
}

PlayTraceBuffer& PlayTraceBuffer::instance() {
    static PlayTraceBuffer buffer;
    return buffer;
}

void PlayTraceBuffer::clear() {
    contractId_.clear();
    contractHash_.clear();
    seed_ = 0;
    startCheckpoint_.clear();
    stepItems_.clear();
    stepCount_ = 0;
    begun_ = false;
}

void PlayTraceBuffer::begin(std::string contractId, std::string contractHash, std::int64_t seed,
                            std::string startCheckpoint) {
    clear();
    contractId_ = std::move(contractId);
    contractHash_ = std::move(contractHash);
    seed_ = seed;
    startCheckpoint_ = std::move(startCheckpoint);
    begun_ = true;
}

void PlayTraceBuffer::append(const Value& request, const Value& response, std::uint64_t hostFrame) {
    if (replaying_) return;
    const auto* object = request.getIf<Value::Object>();
    if (!object) return;
    const auto op = object->find("op");
    if (op == object->end() || !op->second.isString()) return;
    const std::string& name = op->second.asString();
    if (name == "status" || name == "clock" || name == "trace" || name == "replay") return;
    if (!begun_) begin({}, {}, 0, {});
    Value::Object step{{"hostFrame", Value(static_cast<std::int64_t>(hostFrame))},
                       {"request", request},
                       {"seq", Value(++stepCount_)}};
    const auto* responseObject = response.getIf<Value::Object>();
    if (responseObject) {
        const auto state = responseObject->find("state");
        if (state != responseObject->end()) {
            auto digest = playObservationDigest(state->second);
            if (digest) step.emplace("observationDigest", Value(std::move(digest).takeValue()));
        }
        const auto path = responseObject->find("path");
        if (path != responseObject->end() && path->second.isString())
            step.emplace("capture", Value(path->second.asString()));
    }
    stepItems_.emplace_back(Value(std::move(step)));
}

Value PlayTraceBuffer::exportTrace() const {
    return Value(Value::Object{{"contractHash", Value(contractHash_)},
                               {"contractId", Value(contractId_)},
                               {"schemaId", Value(std::string(kTraceSchemaId))},
                               {"schemaVersion", Value(kTraceVersion)},
                               {"seed", Value(seed_)},
                               {"startCheckpoint", Value(startCheckpoint_)},
                               {"steps", Value(stepItems_)}});
}

}  // namespace eve::dev
