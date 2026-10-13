#include "common/GameSaveSession.h"

#include <utility>
#include <vector>

namespace eve {
namespace {

LogicalId gameSaveSchema() {
    const auto schema = LogicalId::parse("game:save-session");
    return schema ? *schema : LogicalId{};
}

}  // namespace

GameSaveSession::GameSaveSession(std::string contentVersion) : contentVersion_(std::move(contentVersion)) {}

Result<void> GameSaveSession::add(IGameSaveParticipant& participant) {
    const std::string key(participant.saveKey());
    if (key.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "game-save participant key is empty", "saveKey"));
    if (!participants_.emplace(key, &participant).second)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate game-save participant", key));
    return Result<void>::success();
}

Result<void> GameSaveSession::addContentMigration(std::string                                fromVersion,
                                                  std::function<Result<Value>(const Value&)> migrate) {
    if (fromVersion.empty() || fromVersion == contentVersion_ || !migrate)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid game-save content migration", "fromVersion"));
    if (!migrations_.emplace(std::move(fromVersion), std::move(migrate)).second)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate game-save content migration", "fromVersion"));
    return Result<void>::success();
}

Result<std::string> GameSaveSession::snapshotJson(const SnapshotHashProvider& hashProvider) const {
    if (contentVersion_.empty() || participants_.empty())
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                              "game save requires content version and participants"));
    Value::Object states;
    for (const auto& [key, participant] : participants_) {
        auto state = participant->capture();
        if (!state.ok()) return Result<std::string>::failure(state.status());
        states.emplace(key, std::move(state).takeValue());
    }
    Value::Object payload{{"contentVersion", Value(contentVersion_)}, {"participants", Value(std::move(states))}};
    auto envelope = makeSnapshotEnvelope("game.save-session", gameSaveSchema(), SchemaVersion(1), PersistentId::nil(),
                                         Revision(0), SimulationTick(0), Value(std::move(payload)), hashProvider);
    if (!envelope.ok()) return Result<std::string>::failure(envelope.status());
    return serializeSnapshotEnvelope(envelope.value());
}

Result<void> GameSaveSession::restoreJson(std::string_view json, const SnapshotHashProvider& hashProvider) {
    if (contentVersion_.empty() || participants_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "game save requires content version and participants"));
    auto decoded = parseSnapshotEnvelope(json, hashProvider);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto& envelope = decoded.value();
    if (envelope.type != "game.save-session" || envelope.schema != gameSaveSchema())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "snapshot is not a game save", "schema"));
    if (envelope.schemaVersion != SchemaVersion(1))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "unknown game-save schema version", "schemaVersion"));
    const auto* payload = envelope.payload.getIf<Value::Object>();
    if (!payload || payload->size() != 2)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "game-save payload requires exactly two fields", "payload"));
    const Value* savedVersion = envelope.payload.find("contentVersion");
    const Value* savedStates  = envelope.payload.find("participants");
    if (!savedVersion || !savedVersion->isString() || !savedStates || !savedStates->isObject())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "invalid game-save payload", "payload"));
    Value migratedStates;
    if (savedVersion->asString() != contentVersion_) {
        const auto migration = migrations_.find(savedVersion->asString());
        if (migration == migrations_.end())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "game-save content version has no migration route",
                                                           "payload.contentVersion"));
        auto migrated = migration->second(*savedStates);
        if (!migrated.ok()) return Result<void>::failure(migrated.status());
        migratedStates = std::move(migrated).takeValue();
        savedStates    = &migratedStates;
    }
    const auto* states = savedStates->getIf<Value::Object>();
    if (!states || states->size() != participants_.size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "game-save participant set differs from the current game",
                                                       "payload.participants"));
    std::vector<std::unique_ptr<IPreparedGameRestore>> prepared;
    prepared.reserve(participants_.size());
    for (const auto& [key, participant] : participants_) {
        const auto state = states->find(key);
        if (state == states->end())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "game-save participant is absent", key));
        auto candidate = participant->prepare(state->second);
        if (!candidate.ok()) return Result<void>::failure(candidate.status());
        if (!candidate.value())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                           "game-save participant returned no prepared state", key));
        prepared.push_back(std::move(candidate).takeValue());
    }
    for (auto& candidate : prepared) candidate->commit();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve
