#include "common/GameSaveSession.h"

#include "game_save_session.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>

#include <limits>
#include <utility>
#include <vector>

namespace eve {
namespace {

LogicalId gameSaveSchema() {
    const auto schema = LogicalId::parse("game:save-session");
    return schema ? *schema : LogicalId{};
}

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index)
                if (hasUnknownFields(reflection->GetRepeatedMessage(message, field, index))) return true;
        } else if (hasUnknownFields(reflection->GetMessage(message, field))) {
            return true;
        }
    }
    return false;
}

bool serializeDeterministic(const google::protobuf::Message& message, std::string& wire) {
    wire.clear();
    google::protobuf::io::StringOutputStream output(&wire);
    google::protobuf::io::CodedOutputStream  coded(&output);
    coded.SetSerializationDeterministic(true);
    return message.SerializeToCodedStream(&coded) && !coded.HadError();
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

Result<void> GameSaveSession::addBinaryContentMigration(
    std::string                                                                            fromVersion,
    std::function<Result<GameSaveParticipantMessages>(const GameSaveParticipantMessages&)> migrate) {
    if (fromVersion.empty() || fromVersion == contentVersion_ || !migrate)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid game-save binary migration", "fromVersion"));
    if (!binaryMigrations_.emplace(std::move(fromVersion), std::move(migrate)).second)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate game-save binary migration", "fromVersion"));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::string> GameSaveSession::snapshotJson(const SnapshotHashProvider& hashProvider) const {
    auto binary = snapshotBinary(hashProvider);
    if (!binary.ok()) return Result<std::string>::failure(binary.status());
    game::proto::GameSaveEnvelope envelope;
    if (!envelope.ParseFromString(binary.value()))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "game save produced invalid Protobuf bytes"));
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(envelope, &json).ok())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "game-save Protobuf JSON conversion failed"));
    return Result<std::string>::success(std::move(json));
}

Result<std::string> GameSaveSession::snapshotBinary(const SnapshotHashProvider& hashProvider) const {
    if (contentVersion_.empty() || participants_.empty() || !hashProvider)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "game save requires content version, participants and digest"));
    game::proto::GameSavePayload payload;
    payload.set_content_version(contentVersion_);
    for (const auto& [key, participant] : participants_) {
        const std::string_view type = participant->messageType();
        if (type.empty() || type.find('/') != std::string_view::npos)
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid participant Protobuf type", key));
        auto captured = participant->captureBinary();
        if (!captured.ok()) return Result<std::string>::failure(captured.status());
        auto& state = (*payload.mutable_participants())[key];
        state.set_type_url("type.googleapis.com/" + std::string(type));
        state.set_value(std::move(captured).takeValue());
    }
    std::string payloadWire;
    if (!serializeDeterministic(payload, payloadWire))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "game-save payload serialization failed"));
    auto digest = hashProvider(payloadWire);
    if (!digest.ok()) return Result<std::string>::failure(digest.status());
    const auto                    digestBytes = digest.value().bytes();
    game::proto::GameSaveEnvelope envelope;
    envelope.set_type("game.save-session");
    envelope.set_schema("game:save-session");
    envelope.set_version(2);
    envelope.set_payload_wire(std::move(payloadWire));
    envelope.set_digest(std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()));
    std::string wire;
    if (!serializeDeterministic(envelope, wire))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "game-save envelope serialization failed"));
    return Result<std::string>::success(std::move(wire));
}

Result<void> GameSaveSession::restoreJson(std::string_view json, const SnapshotHashProvider& hashProvider) {
    game::proto::GameSaveEnvelope protobuf;
    if (google::protobuf::util::JsonStringToMessage(std::string(json), &protobuf).ok()) {
        std::string wire;
        if (!protobuf.SerializeToString(&wire))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "game-save Protobuf JSON is malformed"));
        return restoreBinary(wire, hashProvider);
    }
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

Result<void> GameSaveSession::restoreBinary(std::string_view wire, const SnapshotHashProvider& hashProvider) {
    if (contentVersion_.empty() || participants_.empty() || !hashProvider)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "game save requires content version, participants and digest"));
    if (wire.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "game save is too large"));
    game::proto::GameSaveEnvelope envelope;
    if (!envelope.ParseFromArray(wire.data(), static_cast<int>(wire.size())) || hasUnknownFields(envelope))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "game-save Protobuf envelope is malformed"));
    if (!envelope.has_type() || envelope.type() != "game.save-session" || !envelope.has_schema() ||
        envelope.schema() != "game:save-session")
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "snapshot is not a game save", "schema"));
    if (!envelope.has_version() || envelope.version() != 2)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "unsupported game-save schema version", "version"));
    auto digest = hashProvider(envelope.payload_wire());
    if (!digest.ok()) return Result<void>::failure(digest.status());
    const auto digestBytes = digest.value().bytes();
    if (envelope.digest() != std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "game-save payload digest mismatch", "digest"));
    game::proto::GameSavePayload payload;
    if (!payload.ParseFromString(envelope.payload_wire()) || hasUnknownFields(payload) ||
        !payload.has_content_version() || payload.content_version().empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "game-save payload is malformed"));
    GameSaveParticipantMessages states;
    for (const auto& [key, message] : payload.participants())
        states.emplace(key, GameSaveParticipantMessage{message.type_url(), message.value()});
    if (payload.content_version() != contentVersion_) {
        const auto migration = binaryMigrations_.find(payload.content_version());
        if (migration == binaryMigrations_.end())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "game-save content version has no migration route", "contentVersion"));
        auto migrated = migration->second(states);
        if (!migrated.ok()) return Result<void>::failure(migrated.status());
        states = std::move(migrated).takeValue();
    }
    if (states.size() != participants_.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "game-save participant set differs from the current game", "participants"));
    std::vector<std::unique_ptr<IPreparedGameRestore>> prepared;
    prepared.reserve(participants_.size());
    for (const auto& [key, participant] : participants_) {
        const auto saved = states.find(key);
        if (saved == states.end())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "game-save participant is absent", key));
        if (saved->second.typeUrl != "type.googleapis.com/" + std::string(participant->messageType()))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::UnknownVersion, "game-save participant type differs", key));
        auto candidate = participant->prepareBinary(saved->second.wireBytes);
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
