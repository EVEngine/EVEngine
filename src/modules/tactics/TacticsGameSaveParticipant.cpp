#include "tactics/TacticsGameSaveParticipant.h"

#include "tactics/TacticsPersistence.h"

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

Result<Value> TacticsGameSaveParticipant::capture() const {
    auto snapshot = TacticsPersistence::snapshot(*battle_, hashProvider_);
    if (!snapshot.ok()) return Result<Value>::failure(snapshot.status());
    return snapshotEnvelopeValue(snapshot.value());
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
