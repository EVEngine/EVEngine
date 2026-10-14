#pragma once

#include "common/Export.h"
#include "common/GameSaveSession.h"

namespace eve::rpg {
class RPGSaveSession;

/** @brief Borrow an RPGSaveSession as one atomic game-save participant. */
class EVENGINE_API_PLATFORM RPGGameSaveParticipant final : public IGameSaveParticipant {
public:
    /** @brief The bound session must outlive this adapter and any game-save transaction. */
    explicit RPGGameSaveParticipant(RPGSaveSession& session) noexcept;
    std::string_view                                            saveKey() const noexcept override;
    [[nodiscard]] Result<Value>                                 capture() const override;
    [[nodiscard]] Result<std::unique_ptr<IPreparedGameRestore>> prepare(const Value& state) override;

private:
    RPGSaveSession* session_;
};

}  // namespace eve::rpg
