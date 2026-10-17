#include "rpg/RPGGameSaveParticipant.h"

#include "rpg/RPGSaveSession.h"

#include <utility>

namespace eve::rpg {
namespace {

class PreparedRPGGameRestore final : public IPreparedGameRestore {
public:
    PreparedRPGGameRestore(RPGSaveSession& session, RPGSaveSession::PreparedRestore prepared)
        : session_(session), prepared_(std::move(prepared)) {}
    void commit() noexcept override { session_.commitPrepared(std::move(prepared_)); }

private:
    RPGSaveSession&                 session_;
    RPGSaveSession::PreparedRestore prepared_;
};

}  // namespace

RPGGameSaveParticipant::RPGGameSaveParticipant(RPGSaveSession& session) noexcept : session_(&session) {}

std::string_view RPGGameSaveParticipant::saveKey() const noexcept { return "rpg"; }

std::string_view RPGGameSaveParticipant::messageType() const noexcept { return "eve.rpg.proto.RPGSaveEnvelope"; }

Result<std::string> RPGGameSaveParticipant::captureBinary() const { return session_->snapshotBinary(); }

Result<std::unique_ptr<IPreparedGameRestore>> RPGGameSaveParticipant::prepareBinary(std::string_view wire) {
    auto prepared = session_->prepareRestoreSnapshotBinary(wire);
    if (!prepared.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(prepared.status());
    std::unique_ptr<IPreparedGameRestore> result =
        std::make_unique<PreparedRPGGameRestore>(*session_, std::move(prepared).takeValue());
    return Result<std::unique_ptr<IPreparedGameRestore>>::success(std::move(result));
}

Result<std::unique_ptr<IPreparedGameRestore>> RPGGameSaveParticipant::prepare(const Value& state) {
    auto json = state.toJson();
    if (!json.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(json.status());
    auto prepared = session_->prepareRestoreSnapshotJson(json.value());
    if (!prepared.ok()) return Result<std::unique_ptr<IPreparedGameRestore>>::failure(prepared.status());
    std::unique_ptr<IPreparedGameRestore> result =
        std::make_unique<PreparedRPGGameRestore>(*session_, std::move(prepared).takeValue());
    return Result<std::unique_ptr<IPreparedGameRestore>>::success(std::move(result));
}

}  // namespace eve::rpg
