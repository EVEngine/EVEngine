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

Result<Value> RPGGameSaveParticipant::capture() const {
    auto json = session_->snapshotJson();
    if (!json.ok()) return Result<Value>::failure(json.status());
    return Value::fromJson(json.value());
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
