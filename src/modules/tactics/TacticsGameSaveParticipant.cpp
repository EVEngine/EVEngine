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

std::string_view TacticsGameSaveParticipant::messageType() const noexcept {
    return "eve.tactics.proto.BattleSaveEnvelope";
}

GameSaveMessageTypeMatch TacticsGameSaveParticipant::matchMessageType(std::string_view type) const noexcept {
    if (type == messageType()) return GameSaveMessageTypeMatch::Current;
    if (type == "google.protobuf.BytesValue") return GameSaveMessageTypeMatch::Legacy;
    return GameSaveMessageTypeMatch::Unsupported;
}

Result<std::string> TacticsGameSaveParticipant::captureBinary() const {
    return TacticsPersistence::snapshotBinary(*battle_, hashProvider_);
}

Result<std::unique_ptr<IPreparedGameRestore>> TacticsGameSaveParticipant::prepareBinary(std::string_view wire) {
    auto prepared = TacticsPersistence::prepareRestoreBinary(*battle_, wire, hashProvider_);
    if (!prepared.ok()) {
        const auto typedFailure = prepared.status();
        if (wire.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return Result<std::unique_ptr<IPreparedGameRestore>>::failure(typedFailure);
        google::protobuf::BytesValue legacy;
        if (!legacy.ParseFromArray(wire.data(), static_cast<int>(wire.size())) ||
            !legacy.GetReflection()->GetUnknownFields(legacy).empty())
            return Result<std::unique_ptr<IPreparedGameRestore>>::failure(typedFailure);
        auto snapshot = parseSnapshotEnvelope(legacy.value(), hashProvider_);
        if (!snapshot.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(snapshot.status());
        prepared = TacticsPersistence::prepareRestore(*battle_, snapshot.value(), hashProvider_);
        if (!prepared.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(prepared.status());
    }
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
