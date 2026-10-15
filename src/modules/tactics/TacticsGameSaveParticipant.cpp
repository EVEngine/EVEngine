#include "tactics/TacticsGameSaveParticipant.h"

#include "tactics/TacticsPersistence.h"

#include <google/protobuf/wrappers.pb.h>

#include <limits>
#include <utility>

namespace eve::tactics {
namespace {

class PreparedTacticsGameRestore final : public IPreparedGameRestore {
public:
    explicit PreparedTacticsGameRestore(TacticsPersistence::PreparedRestore prepared)
        : prepared_(std::move(prepared)) {}
    void commit() noexcept override { TacticsPersistence::commitPrepared(std::move(prepared_)); }

private:
    TacticsPersistence::PreparedRestore prepared_;
};

}  // namespace

TacticsGameSaveParticipant::TacticsGameSaveParticipant(Battle& battle, SnapshotHashProvider hashProvider)
    : battle_(&battle), hashProvider_(std::move(hashProvider)) {}

std::string_view TacticsGameSaveParticipant::saveKey() const noexcept { return "tactics"; }

std::string_view TacticsGameSaveParticipant::messageType() const noexcept { return "google.protobuf.BytesValue"; }

Result<std::string> TacticsGameSaveParticipant::captureBinary() const {
    auto snapshot = TacticsPersistence::snapshot(*battle_, hashProvider_);
    if (!snapshot.ok()) return Result<std::string>::failure(snapshot.status());
    auto json = serializeSnapshotEnvelope(snapshot.value());
    if (!json.ok()) return Result<std::string>::failure(json.status());
    google::protobuf::BytesValue message;
    message.set_value(std::move(json).takeValue());
    std::string wire;
    if (!message.SerializeToString(&wire))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "tactics save serialization failed"));
    return Result<std::string>::success(std::move(wire));
}

Result<std::unique_ptr<IPreparedGameRestore>> TacticsGameSaveParticipant::prepareBinary(std::string_view wire) {
    if (wire.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Result<std::unique_ptr<IPreparedGameRestore>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "tactics save is too large"));
    google::protobuf::BytesValue message;
    if (!message.ParseFromArray(wire.data(), static_cast<int>(wire.size())) ||
        !message.GetReflection()->GetUnknownFields(message).empty())
        return Result<std::unique_ptr<IPreparedGameRestore>>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "tactics save wire is malformed"));
    auto snapshot = parseSnapshotEnvelope(message.value(), hashProvider_);
    if (!snapshot.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(snapshot.status());
    auto prepared = TacticsPersistence::prepareRestore(*battle_, snapshot.value(), hashProvider_);
    if (!prepared.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(prepared.status());
    std::unique_ptr<IPreparedGameRestore> result =
        std::make_unique<PreparedTacticsGameRestore>(std::move(prepared).takeValue());
    return Result<std::unique_ptr<IPreparedGameRestore>>::success(std::move(result));
}

Result<std::unique_ptr<IPreparedGameRestore>> TacticsGameSaveParticipant::prepare(const Value& state) {
    auto snapshot = parseSnapshotEnvelopeValue(state, hashProvider_);
    if (!snapshot.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(snapshot.status());
    auto prepared = TacticsPersistence::prepareRestore(*battle_, snapshot.value(), hashProvider_);
    if (!prepared.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(prepared.status());
    std::unique_ptr<IPreparedGameRestore> result =
        std::make_unique<PreparedTacticsGameRestore>(std::move(prepared).takeValue());
    return Result<std::unique_ptr<IPreparedGameRestore>>::success(std::move(result));
}

}  // namespace eve::tactics
